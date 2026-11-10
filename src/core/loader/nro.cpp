// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#ifdef __ANDROID__
#include "common/fs/fs_android.h"
#endif
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/random.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/api_version.h"
#include "core/hle/kernel/svc_common.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/loader/nro.h"
#include "core/loader/nro_romfs.h"
#include "core/memory.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

namespace Loader {

namespace {

bool IsHostContentUri(const std::string& host_filepath) {
#ifdef __ANDROID__
    return Common::FS::Android::IsContentUri(host_filepath);
#else
    return false;
#endif
}

bool LooksLikeSafDocumentArtifactName(const std::string& name) {
    return name.find('%') != std::string::npos || name.find("primary:") != std::string::npos ||
           name.starts_with("root/");
}

void RemoveSafArtifactIfPresent(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    if (!LooksLikeSafDocumentArtifactName(name)) {
        return;
    }
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        std::filesystem::remove(path, ec);
        LOG_INFO(Loader, "Removed stale SAF homebrew sync artifact {}",
                  Common::FS::PathToUTF8String(path));
    }
}

void RemoveStaleSafSyncedHomebrewArtifacts() {
    const auto sdmc_root = Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir);
    const auto switch_dir = sdmc_root / "switch";
    std::error_code ec;
    for (const auto& dir : {sdmc_root, switch_dir}) {
        if (!std::filesystem::is_directory(dir, ec) || ec) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) {
                break;
            }
            RemoveSafArtifactIfPresent(entry.path());
        }
    }
}

std::filesystem::path ResolveHomebrewHostPath(const std::string& host_filepath) {
#ifdef __ANDROID__
    if (Common::FS::Android::IsContentUri(host_filepath)) {
        const std::string resolved = Common::FS::Android::GetHostFilesystemPath(host_filepath);
        if (!resolved.empty()) {
            return std::filesystem::path{resolved};
        }
        const std::string display_name = Common::FS::Android::GetFilename(host_filepath);
        if (!display_name.empty()) {
            return std::filesystem::path{display_name};
        }
    }
#endif
    return std::filesystem::path{host_filepath};
}

std::string BuildHomebrewArgvString(const std::string& host_filepath,
                                    const std::string& extra_program_args) {
    std::filesystem::path canonical_path = ResolveHomebrewHostPath(host_filepath);
    std::error_code ec;
    if (!IsHostContentUri(host_filepath)) {
        const auto resolved = std::filesystem::weakly_canonical(canonical_path, ec);
        if (!ec) {
            canonical_path = resolved;
        }
    } else {
#ifdef __ANDROID__
        const std::string resolved = Common::FS::Android::GetHostFilesystemPath(host_filepath);
        if (!resolved.empty()) {
            const auto resolved_canonical =
                std::filesystem::weakly_canonical(std::filesystem::path{resolved}, ec);
            if (!ec) {
                canonical_path = resolved_canonical;
            } else {
                canonical_path = std::filesystem::path{resolved};
            }
        }
#endif
    }

    const auto& sdmc_root = Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir);
    std::string sdmc_path;
    const auto relative = std::filesystem::relative(canonical_path, sdmc_root, ec);
    const std::string relative_utf8 = Common::FS::PathToUTF8String(relative);
    if (!ec && !relative.empty() && relative_utf8.find("..") == std::string::npos) {
        sdmc_path = "sdmc:/" + Common::FS::PathToUTF8String(relative);
    } else {
        std::string leaf = Common::FS::PathToUTF8String(canonical_path.filename());
#ifdef __ANDROID__
        if (IsHostContentUri(host_filepath)) {
            const std::string display_name = Common::FS::Android::GetFilename(host_filepath);
            if (!display_name.empty()) {
                leaf = display_name;
            }
        }
#endif
        sdmc_path = "sdmc:/switch/" + leaf;
    }
    std::replace(sdmc_path.begin(), sdmc_path.end(), '\\', '/');

    std::string argv_string = sdmc_path;
    if (!extra_program_args.empty()) {
        argv_string += ' ';
        argv_string += extra_program_args;
    }
    argv_string.push_back('\0');
    return argv_string;
}

std::optional<std::filesystem::path> HostPathForSdmcArgv(const std::string& argv_string) {
    const auto end = argv_string.find('\0');
    const std::string_view virtual_path{
        argv_string.data(), end == std::string::npos ? argv_string.size() : end};
    if (!virtual_path.starts_with("sdmc:/")) {
        return std::nullopt;
    }
    const std::string relative = std::string(virtual_path.substr(std::strlen("sdmc:/")));
    if (relative.empty()) {
        return std::nullopt;
    }
    return Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) / relative;
}

void WriteHomebrewNroToHostPath(const FileSys::VirtualFile& nro_file,
                                const std::filesystem::path& dest_path) {
    if (nro_file == nullptr) {
        return;
    }

    std::error_code ec;
    if (std::filesystem::exists(dest_path, ec) && !ec) {
        const auto dest_size = std::filesystem::file_size(dest_path, ec);
        if (!ec && dest_size == nro_file->GetSize()) {
            return;
        }
    }

    if (!Common::FS::CreateParentDirs(dest_path)) {
        LOG_WARNING(Loader, "Failed to create parent directories for {}",
                      Common::FS::PathToUTF8String(dest_path));
        return;
    }

    const auto bytes = nro_file->ReadAllBytes();
    Common::FS::IOFile outfile{dest_path, Common::FS::FileAccessMode::Write,
                               Common::FS::FileType::BinaryFile};
    if (!outfile.IsOpen()) {
        LOG_WARNING(Loader, "Failed to write homebrew NRO to {}",
                      Common::FS::PathToUTF8String(dest_path));
        return;
    }
    if (outfile.Write(bytes) != bytes.size()) {
        LOG_WARNING(Loader, "Incomplete write for homebrew NRO at {}",
                      Common::FS::PathToUTF8String(dest_path));
        return;
    }
    LOG_INFO(Loader, "Synced homebrew NRO to {}", Common::FS::PathToUTF8String(dest_path));
}

void SyncHomebrewNroToSdmc(const FileSys::VirtualFile& nro_file,
                           const std::string& host_filepath) {
    RemoveStaleSafSyncedHomebrewArtifacts();

    const std::string argv_string = BuildHomebrewArgvString(host_filepath, "");
    const auto primary_dest = HostPathForSdmcArgv(argv_string);
    if (!primary_dest) {
        return;
    }

    const std::string dest_leaf = primary_dest->filename().string();
    if (LooksLikeSafDocumentArtifactName(dest_leaf)) {
        LOG_WARNING(Loader, "Refusing to sync homebrew to invalid SAF artifact path {}",
                      Common::FS::PathToUTF8String(*primary_dest));
        return;
    }

    std::filesystem::path source = ResolveHomebrewHostPath(host_filepath);
    std::error_code ec;
    const bool same_file =
        !IsHostContentUri(host_filepath) && std::filesystem::equivalent(source, *primary_dest, ec) &&
        !ec;
    if (!same_file) {
        WriteHomebrewNroToHostPath(nro_file, *primary_dest);
    }

    const std::string filename = Common::FS::PathToUTF8String(source.filename());
    if (filename == "hbmenu.nro") {
        const auto legacy_root =
            Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) / "hbmenu.nro";
        if (legacy_root != *primary_dest) {
            WriteHomebrewNroToHostPath(nro_file, legacy_root);
        }
    }
}

} // namespace

struct NroSegmentHeader {
    u32_le offset;
    u32_le size;
};
static_assert(sizeof(NroSegmentHeader) == 0x8, "NroSegmentHeader has incorrect size.");

struct NroHeader {
    INSERT_PADDING_BYTES(0x4);
    u32_le module_header_offset;
    u32 magic_ext1;
    u32 magic_ext2;
    u32_le magic;
    INSERT_PADDING_BYTES(0x4);
    u32_le file_size;
    INSERT_PADDING_BYTES(0x4);
    std::array<NroSegmentHeader, 3> segments; // Text, RoData, Data (in that order)
    u32_le bss_size;
    INSERT_PADDING_BYTES(0x44);
};
static_assert(sizeof(NroHeader) == 0x80, "NroHeader has incorrect size.");

struct ModHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le unwind_start_offset;
    u32_le unwind_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(ModHeader) == 0x1c, "ModHeader has incorrect size.");

struct AssetSection {
    u64_le offset;
    u64_le size;
};
static_assert(sizeof(AssetSection) == 0x10, "AssetSection has incorrect size.");

struct AssetHeader {
    u32_le magic;
    u32_le format_version;
    AssetSection icon;
    AssetSection nacp;
    AssetSection romfs;
};
static_assert(sizeof(AssetHeader) == 0x38, "AssetHeader has incorrect size.");

static bool IsHomebrewNroHeader(const NroHeader& nro_header) {
    return nro_header.magic_ext1 == Common::MakeMagic('H', 'O', 'M', 'E') &&
           nro_header.magic_ext2 == Common::MakeMagic('B', 'R', 'E', 'W');
}

AppLoader_NRO::AppLoader_NRO(FileSys::VirtualFile file_) : AppLoader(std::move(file_)) {
    NroHeader nro_header{};
    if (file->ReadObject(&nro_header) != sizeof(NroHeader)) {
        return;
    }

    if (file->GetSize() >= nro_header.file_size + sizeof(AssetHeader)) {
        const u64 offset = nro_header.file_size;
        AssetHeader asset_header{};
        if (file->ReadObject(&asset_header, offset) != sizeof(AssetHeader)) {
            return;
        }

        if (asset_header.format_version != 0) {
            LOG_WARNING(Loader,
                        "NRO Asset Header has format {}, currently supported format is 0. If "
                        "strange glitches occur with metadata, check NRO assets.",
                        asset_header.format_version);
        }

        if (asset_header.magic != Common::MakeMagic('A', 'S', 'E', 'T')) {
            return;
        }

        if (asset_header.nacp.size > 0) {
            nacp = std::make_unique<FileSys::NACP>(std::make_shared<FileSys::OffsetVfsFile>(
                file, asset_header.nacp.size, offset + asset_header.nacp.offset, "Control.nacp"));
        }

        if (asset_header.romfs.size > 0) {
            romfs = ExtractEmbeddedRomFsFromNro(file);
            if (romfs != nullptr && !EmbeddedRomFsContainsPath(romfs, "assets.zip") &&
                IsHomebrewNroHeader(nro_header)) {
                LOG_WARNING(Loader,
                            "NRO {} embedded RomFS is missing assets.zip (typical hbmenu failure)",
                            file->GetName());
            }
        }

        if (asset_header.icon.size > 0) {
            icon_data = file->ReadBytes(asset_header.icon.size, offset + asset_header.icon.offset);
        }
    }
}

AppLoader_NRO::~AppLoader_NRO() = default;

FileType AppLoader_NRO::IdentifyType(const FileSys::VirtualFile& nro_file) {
    // Read NSO header
    NroHeader nro_header{};
    if (sizeof(NroHeader) != nro_file->ReadObject(&nro_header)) {
        return FileType::Error;
    }
    if (nro_header.magic == Common::MakeMagic('N', 'R', 'O', '0')) {
        return FileType::NRO;
    }
    return FileType::Error;
}

bool AppLoader_NRO::IsHomebrew() {
    // Read NSO header
    NroHeader nro_header{};
    if (sizeof(NroHeader) != file->ReadObject(&nro_header)) {
        return false;
    }
    return nro_header.magic_ext1 == Common::MakeMagic('H', 'O', 'M', 'E') &&
           nro_header.magic_ext2 == Common::MakeMagic('B', 'R', 'E', 'W');
}

static constexpr u32 PageAlignSize(u32 size) {
    return static_cast<u32>((size + Core::Memory::YUZU_PAGEMASK) & ~Core::Memory::YUZU_PAGEMASK);
}

static bool LoadNroImpl(Core::System& system, Kernel::KProcess& process,
                        const std::vector<u8>& data, const std::string& host_filepath) {
    if (data.size() < sizeof(NroHeader)) {
        return {};
    }

    // Read NSO header
    NroHeader nro_header{};
    std::memcpy(&nro_header, data.data(), sizeof(NroHeader));
    if (nro_header.magic != Common::MakeMagic('N', 'R', 'O', '0')) {
        return {};
    }

    // Build program image
    std::vector<u8> program_image(PageAlignSize(nro_header.file_size));
    std::memcpy(program_image.data(), data.data(), program_image.size());
    if (program_image.size() != PageAlignSize(nro_header.file_size)) {
        return {};
    }

    Kernel::CodeSet codeset;
    for (std::size_t i = 0; i < nro_header.segments.size(); ++i) {
        codeset.segments[i].addr = nro_header.segments[i].offset;
        codeset.segments[i].offset = nro_header.segments[i].offset;
        codeset.segments[i].size = PageAlignSize(nro_header.segments[i].size);
    }

    // Default .bss to NRO header bss size if MOD0 section doesn't exist
    u32 bss_size{PageAlignSize(nro_header.bss_size)};

    // Read MOD header
    ModHeader mod_header{};
    std::memcpy(&mod_header, program_image.data() + nro_header.module_header_offset,
                sizeof(ModHeader));

    const bool has_mod_header{mod_header.magic == Common::MakeMagic('M', 'O', 'D', '0')};
    if (has_mod_header) {
        // Resize program image to include .bss section and page align each section
        bss_size = PageAlignSize(mod_header.bss_end_offset - mod_header.bss_start_offset);
    }

    codeset.DataSegment().size += bss_size;
    program_image.resize(static_cast<u32>(program_image.size()) + bss_size);

    struct ConfigEntry {
        u32_le key;
        u32_le flags;
        u64_le value[2];
    };
    static_assert(sizeof(ConfigEntry) == 0x18);

    // Homebrew ABI (https://switchbrew.org/wiki/Homebrew_ABI)
    constexpr u32 kEntryEndOfList = 0;
    constexpr u32 kEntryMainThreadHandle = 1;
    constexpr u32 kEntryNextLoadPath = 2;
    constexpr u32 kEntryArgv = 5;
    constexpr u32 kEntryAppletType = 7;
    constexpr u32 kAppletTypeApplication = 0;
    constexpr u32 kAppletTypeLibraryApplet = 2;
    constexpr u32 kAppletTypeOverlayApplet = 3;

    // nx-hbloader / libnx expectations for envSetNextLoad / envHasNextLoad.
    constexpr size_t kNextLoadPathBufferSize = 512;
    constexpr size_t kNextLoadArgvBufferSize = 2048;

    constexpr size_t kNumHomebrewEntries = 5; // MainThreadHandle, NextLoadPath, AppletType, Argv, EndOfList
    constexpr size_t kNumProgramArgEntries = 4; // MainThreadHandle, AppletType, Argv, EndOfList
    constexpr size_t kHomebrewConfigTableSize = kNumHomebrewEntries * sizeof(ConfigEntry);
    constexpr size_t kProgramArgConfigTableSize = kNumProgramArgEntries * sizeof(ConfigEntry);

    const bool is_homebrew = nro_header.magic_ext1 == Common::MakeMagic('H', 'O', 'M', 'E') &&
                             nro_header.magic_ext2 == Common::MakeMagic('B', 'R', 'E', 'W');

    // AArch64 encoding for svc #0x7 (ExitProcess).
    constexpr u32 kSvcExitProcessInstruction = 0xD40000E1;
    std::string argv_string;
    size_t homebrew_abi_offset_in_image = 0;
    std::optional<size_t> exit_process_offset_in_image;

    const auto& program_args = Settings::values.program_args.GetValue();
    const std::string effective_host_path = host_filepath;
    const u32 homebrew_applet_type = kAppletTypeApplication;

    if (is_homebrew) {
        argv_string = BuildHomebrewArgvString(effective_host_path, program_args);
    } else if (!program_args.empty()) {
        argv_string = "homebrew ";
        argv_string += program_args;
        argv_string.push_back('\0');
    }

    if (is_homebrew || !argv_string.empty()) {
        const auto& code = codeset.CodeSegment();
        const size_t code_end = (std::min)(program_image.size(), code.offset + code.size);
        for (size_t offset = code.offset; offset + sizeof(u32) <= code_end; offset += sizeof(u32)) {
            u32 instruction{};
            std::memcpy(&instruction, program_image.data() + offset, sizeof(instruction));
            if (instruction == kSvcExitProcessInstruction) {
                exit_process_offset_in_image = offset;
                break;
            }
        }
        if (!exit_process_offset_in_image) {
            LOG_WARNING(Loader,
                        "Unable to find svcExitProcess in NRO; returning from main may fault");
        }
    }

    if (is_homebrew) {
        const size_t homebrew_abi_size = Common::AlignUp(
            kHomebrewConfigTableSize + kNextLoadPathBufferSize + kNextLoadArgvBufferSize +
                argv_string.size(),
            Core::Memory::YUZU_PAGESIZE);

        homebrew_abi_offset_in_image = program_image.size();
        codeset.DataSegment().size += static_cast<u32>(homebrew_abi_size);
        program_image.resize(homebrew_abi_offset_in_image + homebrew_abi_size);
    } else if (!argv_string.empty()) {
        const size_t entries_and_argv =
            Common::AlignUp(kProgramArgConfigTableSize + argv_string.size(), Core::Memory::YUZU_PAGESIZE);

        homebrew_abi_offset_in_image = program_image.size();
        codeset.DataSegment().size += static_cast<u32>(entries_and_argv);
        program_image.resize(homebrew_abi_offset_in_image + entries_and_argv);
    }

    size_t image_size = program_image.size();

#ifdef HAS_NCE
    const auto& code = codeset.CodeSegment();

    // NROs always have a 39-bit address space.
    Settings::SetNceEnabled(true);

    // Create NCE patcher
    Core::NCE::Patcher patch{};

    if (Settings::IsNceEnabled()) {
        // Patch SVCs and MRS calls in the guest code
        patch.PatchText(program_image, code);

        // We only support PostData patching for NROs.
        ASSERT(patch.GetPatchMode() == Core::NCE::PatchMode::PostData);

        // Update patch section.
        auto& patch_segment = codeset.PatchSegment();
        patch_segment.addr = image_size;
        patch_segment.size = static_cast<u32>(patch.GetSectionSize());

        // Add patch section size to the module size.
        image_size += patch_segment.size;
    }
#endif

    // Enable direct memory mapping in case of NCE.
    const u64 fastmem_base = [&]() -> size_t {
        if (Settings::IsNceEnabled()) {
            auto& buffer = system.DeviceMemory().buffer;
            buffer.EnableDirectMappedAddress();
            return reinterpret_cast<u64>(buffer.VirtualBasePointer());
        }
        return 0;
    }();

    // TODO: this is bad form of ASLR, it sucks
    std::uintptr_t aslr_offset = ((::Settings::values.rng_seed_enabled.GetValue()
        ? ::Settings::values.rng_seed.GetValue() : Common::Random::Random64(0)) << 12) & 0xfff000;

    // Setup the process code layout
    if (process
            .LoadFromMetadata(system.Kernel(), FileSys::ProgramMetadata::GetDefault(), image_size, fastmem_base, aslr_offset)
            .IsError()) {
        return false;
    }

    // Relocate code patch and copy to the program_image if running under NCE.
    // This needs to be after LoadFromMetadata so we can use the process entry point.
#ifdef HAS_NCE
    if (Settings::IsNceEnabled()) {
        patch.RelocateAndCopy(process.GetEntryPoint(), code, program_image,
                              &process.GetPostHandlers());
    }
#endif

    // Load codeset for current process
    codeset.memory = std::move(program_image);
    process.LoadModule(system.Kernel(), std::move(codeset), process.GetEntryPoint());

    if (is_homebrew) {
        const u64 base = GetInteger(process.GetEntryPoint());
        const u64 config_addr = base + homebrew_abi_offset_in_image;
        const u64 next_path_addr = config_addr + kHomebrewConfigTableSize;
        const u64 next_argv_addr = next_path_addr + kNextLoadPathBufferSize;
        const u64 host_argv_addr = next_argv_addr + kNextLoadArgvBufferSize;

        const ConfigEntry entries[kNumHomebrewEntries] = {
            {kEntryMainThreadHandle, 0, {0, 0}}, // Value[0] patched in Run()
            {kEntryNextLoadPath, 0, {next_path_addr, next_argv_addr}},
            {kEntryAppletType, 0, {homebrew_applet_type, 0}},
            {kEntryArgv, 0, {0, host_argv_addr}},
            {kEntryEndOfList, 0, {0, 0}},
        };
        process.GetMemory().WriteBlock(Common::ProcessAddress{config_addr}, entries, sizeof(entries));

        // Next-load buffers must start empty; libnx envSetNextLoad fills them when chaining.
        // Pre-filling with the current path made every exit look like a chain request (hbmenu
        // relaunch loop).
        const std::array<u8, 1> empty_string{0};
        process.GetMemory().WriteBlock(Common::ProcessAddress{next_path_addr}, empty_string.data(),
                                       empty_string.size());
        process.GetMemory().WriteBlock(Common::ProcessAddress{next_argv_addr}, empty_string.data(),
                                       empty_string.size());
        process.GetMemory().WriteBlock(Common::ProcessAddress{host_argv_addr}, argv_string.data(),
                                       argv_string.size());

        constexpr size_t kMainThreadHandleValueOffset = offsetof(ConfigEntry, value);
        process.SetArgPointer(Kernel::KProcessAddress{config_addr});
        if (exit_process_offset_in_image) {
            process.SetArgReturnAddress(Kernel::KProcessAddress{base + *exit_process_offset_in_image});
        }
        process.SetMainThreadHandleAddr(
            Kernel::KProcessAddress{config_addr + kMainThreadHandleValueOffset});
    } else if (!argv_string.empty()) {
        const u64 base = GetInteger(process.GetEntryPoint());
        const u64 config_addr = base + homebrew_abi_offset_in_image;
        const u64 argv_addr = config_addr + kProgramArgConfigTableSize;

        const ConfigEntry entries[kNumProgramArgEntries] = {
            {kEntryMainThreadHandle, 0, {0, 0}}, // Value[0] patched in Run()
            {kEntryAppletType, 0, {kAppletTypeApplication, 0}},
            {kEntryArgv, 0, {0, argv_addr}},
            {kEntryEndOfList, 0, {0, 0}},
        };
        process.GetMemory().WriteBlock(Common::ProcessAddress{config_addr}, entries, sizeof(entries));
        process.GetMemory().WriteBlock(Common::ProcessAddress{argv_addr}, argv_string.data(),
                                       argv_string.size());
        constexpr size_t kMainThreadHandleValueOffset = offsetof(ConfigEntry, value);
        process.SetArgPointer(Kernel::KProcessAddress{config_addr});
        if (exit_process_offset_in_image) {
            process.SetArgReturnAddress(Kernel::KProcessAddress{base + *exit_process_offset_in_image});
        }
        process.SetMainThreadHandleAddr(
            Kernel::KProcessAddress{config_addr + kMainThreadHandleValueOffset});
    }

    return true;
}

bool AppLoader_NRO::LoadNro(Core::System& system, Kernel::KProcess& process,
                            const FileSys::VfsFile& nro_file, const std::string& argv,
                            HomebrewAbiOptions abi_options) {
    const std::string& effective_argv =
        !argv.empty() ? argv : system.GetCurrentApplicationFilePath();
    static_cast<void>(abi_options);
    return LoadNroImpl(system, process, nro_file.ReadAllBytes(), effective_argv);
}

AppLoader_NRO::LoadResult AppLoader_NRO::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    if (IsHomebrew()) {
        SyncHomebrewNroToSdmc(file, system.GetCurrentApplicationFilePath());
    }

    if (!LoadNro(system, process, *file)) {
        return {ResultStatus::ErrorLoadingNRO, {}};
    }

    u64 program_id{};
    ReadProgramId(program_id);
    system.GetFileSystemController().RegisterProcess(
        process.GetProcessId(), program_id,
        std::make_unique<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                system.GetFileSystemController()));

    is_loaded = true;
    return {ResultStatus::Success, LoadParameters{Kernel::KThread::DefaultThreadPriority,
                                                  Core::Memory::DEFAULT_STACK_SIZE}};
}

ResultStatus AppLoader_NRO::ReadIcon(std::vector<u8>& buffer) {
    if (icon_data.empty()) {
        return ResultStatus::ErrorNoIcon;
    }

    buffer = icon_data;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadProgramId(u64& out_program_id) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    out_program_id = nacp->GetTitleId();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadRomFS(FileSys::VirtualFile& dir) {
    if (romfs == nullptr) {
        return ResultStatus::ErrorNoRomFS;
    }

    dir = romfs;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadTitle(std::string& title) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    title = nacp->GetApplicationName();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadControlData(FileSys::NACP& control) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    control = *nacp;
    return ResultStatus::Success;
}

bool AppLoader_NRO::IsRomFSUpdatable() const {
    return false;
}

} // namespace Loader

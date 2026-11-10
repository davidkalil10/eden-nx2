// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/loader/nro_romfs.h"

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "common/fs/path_util.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_offset.h"

namespace Loader {

namespace {

struct AssetSection {
    u64_le offset;
    u64_le size;
};

struct AssetHeader {
    u32_le magic;
    u32_le format_version;
    AssetSection icon;
    AssetSection nacp;
    AssetSection romfs;
};

struct NroHeader {
    u32_le unused;
    u32_le module_header_offset;
    u32 magic_ext1;
    u32 magic_ext2;
    u32_le magic;
    u32_le unk1;
    u32_le file_size;
};

} // namespace

FileSys::VirtualFile ExtractEmbeddedRomFsFromNro(const FileSys::VirtualFile& nro_file) {
    if (nro_file == nullptr) {
        return nullptr;
    }

    NroHeader nro_header{};
    if (nro_file->ReadObject(&nro_header) != sizeof(NroHeader)) {
        return nullptr;
    }

    if (nro_header.magic != Common::MakeMagic('N', 'R', 'O', '0')) {
        return nullptr;
    }

    if (nro_file->GetSize() < nro_header.file_size + sizeof(AssetHeader)) {
        return nullptr;
    }

    const u64 asset_offset = nro_header.file_size;
    AssetHeader asset_header{};
    if (nro_file->ReadObject(&asset_header, asset_offset) != sizeof(AssetHeader)) {
        return nullptr;
    }

    if (asset_header.magic != Common::MakeMagic('A', 'S', 'E', 'T')) {
        return nullptr;
    }

    if (asset_header.romfs.size == 0 || asset_header.romfs.offset == 0) {
        return nullptr;
    }

    return std::make_shared<FileSys::OffsetVfsFile>(nro_file, asset_header.romfs.size,
                                                    asset_offset + asset_header.romfs.offset,
                                                    "game.romfs");
}

bool EmbeddedRomFsContainsPath(const FileSys::VirtualFile& romfs_image, const std::string& path) {
    if (romfs_image == nullptr || path.empty()) {
        return false;
    }

    const auto root = FileSys::ExtractRomFS(romfs_image);
    if (root == nullptr) {
        return false;
    }

    const auto sanitized = Common::FS::SanitizePath(path);
    return root->GetFileRelative(sanitized) != nullptr;
}

std::string NormalizeHomebrewNroArgvPath(const std::string& sdmc_path) {
    if (sdmc_path.empty()) {
        return {};
    }

    if (sdmc_path.rfind("sdmc:", 0) == 0) {
        return sdmc_path;
    }

    const auto switch_pos = sdmc_path.find("switch/");
    if (switch_pos != std::string::npos) {
        return "sdmc:/" + sdmc_path.substr(switch_pos);
    }

    const auto switch_pos_backslash = sdmc_path.find("switch\\");
    if (switch_pos_backslash != std::string::npos) {
        std::string tail = sdmc_path.substr(switch_pos_backslash);
        for (char& ch : tail) {
            if (ch == '\\') {
                ch = '/';
            }
        }
        return "sdmc:/" + tail;
    }

    std::string normalized = sdmc_path;
    while (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }

    if (normalized.empty()) {
        return {};
    }

    return "sdmc:/" + normalized;
}

} // namespace Loader

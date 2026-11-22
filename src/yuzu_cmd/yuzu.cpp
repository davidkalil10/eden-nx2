// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#ifdef __SWITCH2__
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <tuple>
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "common/overlay_panels.h"
#include "common/thread.h"
#include "core/perf_stats.h"
#include "dynarmic/backend/arm64/switch2_code_block.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "input_common/drivers/touch_screen.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "yuzu_cmd/switch2_launcher.h"
#include "yuzu_cmd/switch2_runtime.h"

extern "C" {
extern int g_switch2_address_space_mode;
extern int g_switch2_jit_blocks_ahead;
extern std::atomic<std::uint64_t> g_switch2_stat_jit_blocks;
extern std::atomic<std::uint64_t> g_switch2_stat_icache_inv;
extern std::atomic<std::uint64_t> g_switch2_stat_excl_cb;
extern std::atomic<std::uint64_t> g_switch2_stat_svcs;
}

static void Switch2BootTrace(const char* stage) {
    std::error_code ec;
    std::filesystem::create_directories("sdmc:/switch/eden-nx2", ec);
    if (FILE* f = std::fopen("sdmc:/switch/eden-nx2/boot_trace.txt", "a")) {
        std::fprintf(f, "%s\n", stage);
        std::fflush(f);
        std::fclose(f);
    }
}
#endif
#include "common/settings_enums.h"
#include "core/launch_params.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#ifndef __SWITCH2__
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#endif

#include <format>

#include "common/logging.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/cpu_manager.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "frontend_common/config.h"
#include "input_common/main.h"
#include "network/network.h"
#include "sdl_config.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3.h"
#ifdef HAS_OPENGL
#include "yuzu_cmd/emu_window/emu_window_sdl3_gl.h"
#endif
#include "yuzu_cmd/emu_window/emu_window_sdl3_null.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3_vk.h"

#ifdef _WIN32
// windows.h needs to be included before shellapi.h
#include <windows.h>
#include <shellapi.h>
#include "common/windows/timer_resolution.h"
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia and AMD drivers to use the dedicated GPU by default on laptops with switchable
// graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#ifndef __SWITCH2__
static void OnStateChanged(const Network::RoomMember::State& state) {
    switch (state) {
    case Network::RoomMember::State::Idle:
        LOG_DEBUG(Network, "Network is idle");
        break;
    case Network::RoomMember::State::Joining:
        LOG_DEBUG(Network, "Connection sequence to room started");
        break;
    case Network::RoomMember::State::Joined:
        LOG_DEBUG(Network, "Successfully joined to the room");
        break;
    case Network::RoomMember::State::Moderator:
        LOG_DEBUG(Network, "Successfully joined the room as a moderator");
        break;
    default:
        break;
    }
}

static void OnNetworkError(const Network::RoomMember::Error& error) {
    switch (error) {
    case Network::RoomMember::Error::LostConnection:
        LOG_DEBUG(Network, "Lost connection to the room");
        break;
    case Network::RoomMember::Error::CouldNotConnect:
        LOG_ERROR(Network, "Error: Could not connect");
        exit(1);
        break;
    case Network::RoomMember::Error::NameCollision:
        LOG_ERROR(
            Network,
            "You tried to use the same nickname as another user that is connected to the Room");
        exit(1);
        break;
    case Network::RoomMember::Error::IpCollision:
        LOG_ERROR(Network, "You tried to use the same fake IP-Address as another user that is "
                           "connected to the Room");
        exit(1);
        break;
    case Network::RoomMember::Error::WrongPassword:
        LOG_ERROR(Network, "Room replied with: Wrong password");
        exit(1);
        break;
    case Network::RoomMember::Error::WrongVersion:
        LOG_ERROR(Network,
                  "You are using a different version than the room you are trying to connect to");
        exit(1);
        break;
    case Network::RoomMember::Error::RoomIsFull:
        LOG_ERROR(Network, "The room is full");
        exit(1);
        break;
    case Network::RoomMember::Error::HostKicked:
        LOG_ERROR(Network, "You have been kicked by the host");
        break;
    case Network::RoomMember::Error::HostBanned:
        LOG_ERROR(Network, "You have been banned by the host");
        break;
    case Network::RoomMember::Error::UnknownError:
        LOG_ERROR(Network, "UnknownError");
        break;
    case Network::RoomMember::Error::PermissionDenied:
        LOG_ERROR(Network, "PermissionDenied");
        break;
    case Network::RoomMember::Error::NoSuchUser:
        LOG_ERROR(Network, "NoSuchUser");
        break;
    }
}

static void OnMessageReceived(const Network::ChatEntry& msg) {
    std::cout << std::endl << msg.nickname << ": " << msg.message << std::endl << std::endl;
}

static void OnStatusMessageReceived(const Network::StatusMessageEntry& msg) {
    std::string message = [&]() {
        switch (msg.type) {
        case Network::IdMemberJoin:
            return std::format("{} has joined", msg.nickname);
        case Network::IdMemberLeave:
            return std::format("{} has left", msg.nickname);
        case Network::IdMemberKicked:
            return std::format("{} has been kicked", msg.nickname);
        case Network::IdMemberBanned:
            return std::format("{} has been banned", msg.nickname);
        case Network::IdAddressUnbanned:
            return std::format("{} has been unbanned", msg.nickname);
        default:
            return std::string{};
        }
    }();
    if (!message.empty())
        std::cout << std::endl << "* " << message << std::endl << std::endl;
}
#endif

#ifdef __SWITCH2__

namespace {

class DummyContext final : public Core::Frontend::GraphicsContext {};

class EmuWindowSwitch2 final : public Core::Frontend::EmuWindow {
public:
    EmuWindowSwitch2() {
        std::uint32_t w = 1280;
        std::uint32_t h = 720;
        Switch2GetWindowDimensions(&w, &h);
        window_info.type = Core::Frontend::WindowSystemType::Switch2;
        window_info.render_surface = Switch2GetNativeWindow();
        NotifyClientAreaSizeChanged({w, h});
        UpdateCurrentFramebufferLayout(w, h);
    }

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<DummyContext>();
    }

    bool IsShown() const override {
        return true;
    }
};

void UpdateSwitch2FpsOverlay(const std::string& text) {
    auto& panel = Common::GetOverlayPanels()[0];
    std::scoped_lock lk{panel.mutex};
    if (text.empty()) {
        panel.enabled = false;
        panel.pixels.clear();
        panel.width = 0;
        panel.height = 0;
        ++panel.version;
        return;
    }

    struct Glyph {
        char ch;
        std::array<std::string_view, 7> rows;
    };
    static constexpr std::array<Glyph, 35> kGlyphs{{
        {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
        {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
        {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
        {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
        {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
        {'5', {"11111", "10000", "11110", "00001", "00001", "10001", "01110"}},
        {'6', {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}},
        {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
        {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
        {'9', {"01110", "10001", "10001", "01111", "00001", "00010", "01100"}},
        {'.', {"00000", "00000", "00000", "00000", "00000", "01100", "01100"}},
        {':', {"00000", "00100", "00100", "00000", "00100", "00100", "00000"}},
        {'>', {"10000", "01000", "00100", "00010", "00100", "01000", "10000"}},
        {'%', {"11001", "11010", "00010", "00100", "01000", "01011", "10011"}},
        {' ', {"00000", "00000", "00000", "00000", "00000", "00000", "00000"}},
        {'|', {"00100", "00100", "00100", "00100", "00100", "00100", "00100"}},
        {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
        {'C', {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}},
        {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
        {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
        {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
        {'G', {"01110", "10001", "10000", "10111", "10001", "10001", "01110"}},
        {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
        {'I', {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}},
        {'J', {"00111", "00010", "00010", "00010", "10010", "10010", "01100"}},
        {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
        {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
        {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
        {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
        {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
        {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
        {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
        {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
        {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
        {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
    }};
    auto find_glyph = [&](char c) -> const Glyph* {
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (const auto& g : kGlyphs) {
            if (g.ch == up) return &g;
        }
        return nullptr;
    };

    std::vector<std::string_view> lines;
    std::size_t longest = 0;
    for (std::size_t pos = 0; pos <= text.size();) {
        const std::size_t nl = std::min(text.find('\n', pos), text.size());
        lines.push_back(std::string_view(text).substr(pos, nl - pos));
        longest = std::max(longest, nl - pos);
        pos = nl + 1;
    }

    constexpr u32 kScale = 3;
    constexpr u32 kPad = 8;
    constexpr u32 kCellW = 6 * kScale;
    constexpr u32 kLineH = 9 * kScale;
    const u32 width = static_cast<u32>(longest) * kCellW + kPad * 2;
    const u32 height = static_cast<u32>(lines.size()) * kLineH + kPad * 2;

    panel.pixels.assign(std::size_t{width} * height * 4, 0);
    for (std::size_t i = 3; i < panel.pixels.size(); i += 4) {
        panel.pixels[i] = 165;
    }

    for (std::size_t l = 0; l < lines.size(); ++l) {
        for (std::size_t c = 0; c < lines[l].size(); ++c) {
            const Glyph* g = find_glyph(lines[l][c]);
            if (!g) continue;
            const u32 ox = kPad + static_cast<u32>(c) * kCellW;
            const u32 oy = kPad + static_cast<u32>(l) * kLineH;
            for (u32 gy = 0; gy < 7 * kScale; ++gy) {
                if (g->rows[gy / kScale][0] == '\0') continue;
                for (u32 gx = 0; gx < 5 * kScale; ++gx) {
                    if (g->rows[gy / kScale][gx / kScale] != '1') continue;
                    u8* px = &panel.pixels[(std::size_t{oy + gy} * width + ox + gx) * 4];
                    px[0] = 240;
                    px[1] = 255;
                    px[2] = 245;
                    px[3] = 255;
                }
            }
        }
    }
    panel.width = width;
    panel.height = height;
    panel.anchor = Common::OverlayAnchor::TopLeft;
    panel.drop = 0.0f;
    panel.enabled = true;
    ++panel.version;
}

void ScanSwitch2AddonDir(FileSys::ManualContentProvider& provider, FileSys::VfsFilesystem& vfs,
                         const std::string& dir, const std::string& skip_path, bool filter_upd,
                         bool filter_dlc, bool recurse_one) {
    try {
        std::error_code ec;
        if (dir.empty() || !std::filesystem::is_directory(dir, ec) || ec) return;
        std::filesystem::directory_iterator it(dir, ec);
        const std::filesystem::directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const auto& entry = *it;
            const std::string p = entry.path().generic_string();
            if (entry.is_directory(ec)) {
                if (recurse_one) {
                    const std::string dname = entry.path().filename().string();
                    if (!dname.empty() && dname[0] != '.') {
                        ScanSwitch2AddonDir(provider, vfs, p, skip_path, filter_upd, filter_dlc,
                                            false);
                    }
                }
                continue;
            }
            if (!entry.is_regular_file(ec) || p == skip_path) continue;
            const std::string ext = Common::ToLower(entry.path().extension().string());
            if (ext != ".nsp" && ext != ".nsz" && ext != ".xci" && ext != ".xcz") continue;
            const std::string lower_name = Common::ToLower(entry.path().filename().string());
            if (filter_upd &&
                lower_name.find("upd") == std::string::npos &&
                lower_name.find("patch") == std::string::npos &&
                lower_name.find("[v") == std::string::npos) {
                continue;
            }
            if (filter_dlc &&
                lower_name.find("dlc") == std::string::npos &&
                lower_name.find("aoc") == std::string::npos) {
                continue;
            }
            if (auto f = vfs.OpenFile(p, FileSys::OpenMode::Read)) {
                provider.AddEntriesFromContainer(f, false);
            }
        }
    } catch (...) {
    }
}

void RegisterSwitch2Addons(FileSys::ManualContentProvider& provider, FileSys::VfsFilesystem& vfs,
                           const Switch2LaunchConfig& cfg) {
    auto add_file = [&](const std::string& path) {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec)) return;
        if (auto f = vfs.OpenFile(path, FileSys::OpenMode::Read)) {
            provider.AddEntriesFromContainer(f, false);
        }
    };

    const std::string rom_dir = std::filesystem::path(cfg.rom_path).parent_path().generic_string();

    if (cfg.update_pkg == "auto" || cfg.update_pkg.empty()) {
        ScanSwitch2AddonDir(provider, vfs, "sdmc:/switch/eden-nx2/updates", cfg.rom_path, false,
                            false, true);
        ScanSwitch2AddonDir(provider, vfs, rom_dir, cfg.rom_path, true, false, false);
    } else if (cfg.update_pkg != "none") {
        add_file(cfg.update_pkg);
    }

    if (cfg.dlc_pkg == "auto" || cfg.dlc_pkg.empty()) {
        ScanSwitch2AddonDir(provider, vfs, "sdmc:/switch/eden-nx2/dlc", cfg.rom_path, false, false,
                            true);
        ScanSwitch2AddonDir(provider, vfs, rom_dir, cfg.rom_path, false, true, false);
    } else if (cfg.dlc_pkg != "none") {
        std::size_t start = 0;
        while (start < cfg.dlc_pkg.size()) {
            const std::size_t bar = cfg.dlc_pkg.find('|', start);
            const std::string item = (bar == std::string::npos)
                                         ? cfg.dlc_pkg.substr(start)
                                         : cfg.dlc_pkg.substr(start, bar - start);
            if (!item.empty()) {
                std::error_code ec;
                if (std::filesystem::is_directory(item, ec)) {
                    ScanSwitch2AddonDir(provider, vfs, item, cfg.rom_path, false, false, true);
                } else {
                    add_file(item);
                }
            }
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
    }
}

void ApplySwitch2LaunchConfig(const Switch2LaunchConfig& cfg, std::size_t& active_pad_index,
                              Core::HID::NpadIdType& active_npad_id) {
    Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
    Settings::values.cpu_backend.SetValue(Settings::CpuBackend::Dynarmic);
    Settings::values.cpuopt_fastmem.SetValue(false);
    Settings::values.cpu_accuracy.SetValue(cfg.cpu_unsafe ? Settings::CpuAccuracy::Unsafe
                                                          : Settings::CpuAccuracy::Auto);
    Settings::values.use_disk_shader_cache.SetValue(cfg.shader_cache);
    Settings::values.frame_gen.SetValue(cfg.frame_gen_mult >= 2);
    if (cfg.frame_gen_mult >= 2) {
        Settings::values.frame_gen_multiplier.SetValue(static_cast<u32>(cfg.frame_gen_mult));
    }

    g_switch2_address_space_mode = cfg.address_space_mode;
    g_switch2_jit_blocks_ahead = cfg.jit_blocks_ahead;
    Switch2SetCpuBoost(cfg.cpu_boost);

    // Resolution scale
    Settings::ResolutionSetup res_setup = Settings::ResolutionSetup::Res1X;
    if (cfg.resolution_scale <= 0.6f) res_setup = Settings::ResolutionSetup::Res1_2X;
    else if (cfg.resolution_scale <= 0.85f) res_setup = Settings::ResolutionSetup::Res3_4X;
    else if (cfg.resolution_scale >= 1.8f) res_setup = Settings::ResolutionSetup::Res2X;
    else if (cfg.resolution_scale >= 1.3f) res_setup = Settings::ResolutionSetup::Res3_2X;
    Settings::values.resolution_setup.SetValue(res_setup);

    // GPU Upscaler (Scaling Filter) & Anti-Aliasing
    static constexpr std::array<Settings::ScalingFilter, 7> kScalingFilters{{
        Settings::ScalingFilter::Bilinear,
        Settings::ScalingFilter::Fsr,
        Settings::ScalingFilter::Sgsr,
        Settings::ScalingFilter::ScaleForce,
        Settings::ScalingFilter::Mmpx,
        Settings::ScalingFilter::Lanczos,
        Settings::ScalingFilter::NearestNeighbor,
    }};
    const int sf_idx = std::clamp(cfg.scaling_filter, 0, static_cast<int>(kScalingFilters.size()) - 1);
    Settings::values.scaling_filter.SetValue(kScalingFilters[static_cast<std::size_t>(sf_idx)]);
    Settings::values.fsr_sharpening_slider.SetValue(40);

    const Settings::AntiAliasing aa_mode =
        (cfg.anti_aliasing == 1)
            ? Settings::AntiAliasing::Fxaa
            : (cfg.anti_aliasing == 2 ? Settings::AntiAliasing::Smaa
                                      : Settings::AntiAliasing::None);
    Settings::values.anti_aliasing.SetValue(aa_mode);

    // Vulkan presentation, dynamic state & Switch 2 unified memory tuning
    Settings::values.async_presentation.SetValue(true);
    const Settings::VSyncMode vsync =
        (cfg.vsync_mode == 1)
            ? Settings::VSyncMode::Immediate
            : (cfg.vsync_mode == 2
                   ? Settings::VSyncMode::Mailbox
                   : (cfg.vsync_mode == 3 ? Settings::VSyncMode::Fifo
                                          : Settings::VSyncMode::FifoRelaxed));
    Settings::values.vsync_mode.SetValue(vsync);
    if (cfg.speed_limit <= 0) {
        Settings::values.use_speed_limit.SetValue(false);
    } else {
        Settings::values.use_speed_limit.SetValue(true);
        Settings::values.speed_limit.SetValue(static_cast<u16>(cfg.speed_limit));
    }
    Settings::values.dyna_state.SetValue(Settings::ExtendedDynamicState::EDS3);
    Settings::values.vram_usage_mode.SetValue(Settings::VramUsageMode::Aggressive);
    Settings::values.gpu_clock.SetValue(Settings::GpuClock::Boost);

    // GPU accuracy & performance flags
    if (cfg.gpu_fast) {
        Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::Low);
        Settings::values.use_reactive_flushing.SetValue(false);
        Settings::values.use_asynchronous_shaders.SetValue(true);
        Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
        Settings::values.dma_accuracy.SetValue(Settings::DmaAccuracy::Unsafe);
        Settings::values.barrier_feedback_loops.SetValue(false);
        Settings::values.skip_cpu_inner_invalidation.SetValue(true);
    } else {
        Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::High);
        Settings::values.use_reactive_flushing.SetValue(true);
        Settings::values.use_asynchronous_shaders.SetValue(true);
        Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
        Settings::values.skip_cpu_inner_invalidation.SetValue(false);
    }

    // Language & Region
    static constexpr std::array<std::tuple<std::string_view, Settings::Language, Settings::Region>, 8> kLangs{{
        {"pt-BR", Settings::Language::PortugueseBrazilian, Settings::Region::Usa},
        {"en-US", Settings::Language::EnglishAmerican, Settings::Region::Usa},
        {"es-419", Settings::Language::SpanishLatin, Settings::Region::Usa},
        {"es-ES", Settings::Language::Spanish, Settings::Region::Europe},
        {"fr", Settings::Language::French, Settings::Region::Europe},
        {"de", Settings::Language::German, Settings::Region::Europe},
        {"it", Settings::Language::Italian, Settings::Region::Europe},
        {"ja", Settings::Language::Japanese, Settings::Region::Japan},
    }};
    for (const auto& [code, lang, reg] : kLangs) {
        if (cfg.language == code) {
            Settings::values.language_index.SetValue(lang);
            Settings::values.region_index.SetValue(reg);
            break;
        }
    }

    // Disconnect all emulated controllers first so profiles apply cleanly
    auto& players = Settings::values.players.GetValue();
    for (auto& p : players) {
        p.connected = false;
    }

    if (cfg.controller_profile == Switch2ControllerProfile::Handheld) {
        Settings::values.use_docked_mode.SetValue(Settings::ConsoleMode::Handheld);
        players[8].connected = true;
        players[8].controller_type = Settings::ControllerType::Handheld;
        active_pad_index = 8;
        active_npad_id = Core::HID::NpadIdType::Handheld;
    } else {
        Settings::values.use_docked_mode.SetValue(cfg.docked_mode ? Settings::ConsoleMode::Docked
                                                                  : Settings::ConsoleMode::Handheld);
        Settings::ControllerType ctrl_type = Settings::ControllerType::ProController;
        switch (cfg.controller_profile) {
        case Switch2ControllerProfile::DualJoycon:
            ctrl_type = Settings::ControllerType::DualJoyconDetached;
            break;
        case Switch2ControllerProfile::RightJoycon:
            ctrl_type = Settings::ControllerType::RightJoycon;
            break;
        case Switch2ControllerProfile::LeftJoycon:
            ctrl_type = Settings::ControllerType::LeftJoycon;
            break;
        default:
            ctrl_type = Settings::ControllerType::ProController;
            break;
        }
        players[0].connected = true;
        players[0].controller_type = ctrl_type;
        active_pad_index = 0;
        active_npad_id = Core::HID::NpadIdType::Player1;
    }

    Settings::UpdateRescalingInfo();
}

void ForwardSwitch2Input(InputCommon::InputSubsystem& input_subsystem,
                         const Switch2PadSnapshot& pad, Switch2PadSnapshot& prev,
                         std::size_t pad_index, Switch2ControllerProfile profile,
                         bool& prev_touch) {
    auto* gamepad = input_subsystem.GetVirtualGamepad();
    if (gamepad != nullptr) {
        const std::uint32_t changed = pad.buttons ^ prev.buttons;
        for (int button = 0; button < 16; ++button) {
            if (changed & (1u << button)) {
                gamepad->SetButtonState(pad_index, button, (pad.buttons & (1u << button)) != 0);
            }
        }

        // SL/SR mapping (physical SL/SR bits 16/17, plus L/ZL as SL and R/ZR as SR for single Joy-Con comfort)
        const bool single_jc = (profile == Switch2ControllerProfile::RightJoycon ||
                                profile == Switch2ControllerProfile::LeftJoycon);
        const auto calc_sl = [&](std::uint32_t btns) {
            return (btns & (1u << 16)) != 0 || (single_jc && (btns & (1u << 8)) != 0);
        };
        const auto calc_sr = [&](std::uint32_t btns) {
            return (btns & (1u << 17)) != 0 || (single_jc && (btns & (1u << 9)) != 0);
        };
        const bool sl_now = calc_sl(pad.buttons);
        const bool sl_prev = calc_sl(prev.buttons);
        if (sl_now != sl_prev) {
            gamepad->SetButtonState(pad_index, 16, sl_now); // SLLeft
            gamepad->SetButtonState(pad_index, 20, sl_now); // SLRight
        }
        const bool sr_now = calc_sr(pad.buttons);
        const bool sr_prev = calc_sr(prev.buttons);
        if (sr_now != sr_prev) {
            gamepad->SetButtonState(pad_index, 17, sr_now); // SRLeft
            gamepad->SetButtonState(pad_index, 21, sr_now); // SRRight
        }

        // Sticks (for RightJoycon single mode, mirror left stick to right stick if right stick is idle)
        float rx = pad.right_x;
        float ry = pad.right_y;
        if (profile == Switch2ControllerProfile::RightJoycon &&
            std::fabs(rx) < 0.12f && std::fabs(ry) < 0.12f) {
            rx = pad.left_x;
            ry = pad.left_y;
        }
        if (pad.left_x != prev.left_x || pad.left_y != prev.left_y) {
            gamepad->SetStickPosition(pad_index, InputCommon::VirtualGamepad::VirtualStick::Left,
                                      pad.left_x, pad.left_y);
        }
        if (rx != prev.right_x || ry != prev.right_y ||
            (profile == Switch2ControllerProfile::RightJoycon &&
             (pad.left_x != prev.left_x || pad.left_y != prev.left_y))) {
            gamepad->SetStickPosition(pad_index, InputCommon::VirtualGamepad::VirtualStick::Right,
                                      rx, ry);
        }

        if (pad.has_motion) {
            gamepad->SetMotionState(pad_index, pad.motion_delta_us, pad.gyro[0], pad.gyro[1],
                                    pad.gyro[2], pad.accel[0], pad.accel[1], pad.accel[2]);
        }
    }

    if (auto* touch = input_subsystem.GetTouchScreen()) {
        if (pad.has_touch) {
            if (!prev_touch) {
                touch->TouchPressed(pad.touch_x, pad.touch_y, 0);
            } else {
                touch->TouchMoved(pad.touch_x, pad.touch_y, 0);
            }
            prev_touch = true;
        } else if (prev_touch) {
            touch->ReleaseAllTouch();
            prev_touch = false;
        }
    }

    prev = pad;
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    std::set_terminate([]() {
        std::error_code ec;
        std::filesystem::create_directories("sdmc:/switch/eden-nx2", ec);
        if (FILE* f = std::fopen("sdmc:/switch/eden-nx2/crash.txt", "a")) {
            if (auto ex = std::current_exception()) {
                try {
                    std::rethrow_exception(ex);
                } catch (const std::exception& e) {
                    std::fprintf(f, "std::terminate: %s\n", e.what());
                } catch (...) {
                    std::fprintf(f, "std::terminate: unknown exception\n");
                }
            } else {
                std::fprintf(f, "std::terminate: no active exception\n");
            }
            std::fflush(f);
            std::fclose(f);
        }
        std::abort();
    });

    {
        std::error_code ec;
        std::filesystem::create_directories("sdmc:/switch/eden-nx2", ec);
        if (FILE* f = std::fopen("sdmc:/switch/eden-nx2/boot_trace.txt", "w")) {
            std::fprintf(f, "00: main enter (argc=%d)\n", argc);
            std::fflush(f);
            std::fclose(f);
        }
    }

    Settings::values.log_flush_line.SetValue(true);
    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(false);
    Common::Log::Start();
    Switch2BootTrace("01: Log started");

    Switch2InitInput();

    Switch2LaunchConfig launch_cfg{};
    if (!Switch2RunLauncher(&launch_cfg, argc > 0 ? argv[0] : nullptr) || launch_cfg.rom_path.empty()) {
        Switch2BootTrace(launch_cfg.request_restart
                             ? "02: Launcher requested restart after update"
                             : "02: Launcher exited without launching a ROM");
        Common::Log::Stop();
        Dynarmic::Backend::Arm64::Switch2JitPool::Shutdown();
        if (launch_cfg.request_restart) {
            Switch2ScheduleReturnToLauncher(argc > 0 ? argv[0] : nullptr);
        }
        return 0;
    }

    {
        std::string msg = "02: ROM=" + launch_cfg.rom_path;
        Switch2BootTrace(msg.c_str());
    }

    std::size_t active_pad_index = 0;
    Core::HID::NpadIdType active_npad_id = Core::HID::NpadIdType::Player1;
    ApplySwitch2LaunchConfig(launch_cfg, active_pad_index, active_npad_id);

    auto manual_provider = std::make_unique<FileSys::ManualContentProvider>();
    auto system = std::make_unique<Core::System>();
    auto input_subsystem = std::make_unique<InputCommon::InputSubsystem>();
    auto emu_window = std::make_unique<EmuWindowSwitch2>();
    Switch2BootTrace("03: System/Window created");

    system->Initialize();
    system->ApplySettings();
    input_subsystem->Initialize();
    system->HIDCore().ReloadInputDevices();
    auto content_provider = std::make_unique<FileSys::ContentProviderUnion>();
    content_provider->SetSlot(FileSys::ContentProviderUnionSlot::FrontendManual,
                              manual_provider.get());
    system->SetContentProvider(std::move(content_provider));
    system->SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    system->GetFileSystemController().CreateFactories(*system->GetFilesystem());
    RegisterSwitch2Addons(*manual_provider, *system->GetFilesystem(), launch_cfg);
    system->GetUserChannel().clear();
    Switch2BootTrace("04: System initialized");

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    const Core::SystemResultStatus load_result =
        system->Load(*emu_window, launch_cfg.rom_path, load_parameters);
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "05: system.Load result=%d", static_cast<int>(load_result));
        Switch2BootTrace(buf);
    }

    bool should_return_to_launcher = false;
    if (load_result == Core::SystemResultStatus::Success) {
        system->GPU().Start();
        system->GetCpuManager().OnGpuReady();

        if (Settings::values.use_disk_shader_cache.GetValue()) {
            system->Renderer().ReadRasterizer()->LoadDiskResources(
                system->GetApplicationProcessProgramID(), std::stop_token{},
                [](VideoCore::LoadCallbackStage, size_t, size_t) {});
        }

        // Disable per-line synchronous SD flush during gameplay for smoothness.
        Settings::values.log_flush_line.SetValue(false);
        Settings::values.log_filter.SetValue("*:Error");
        Common::Log::Initialize();

        system->RegisterExitCallback([] {});
        void(system->Run());
        Switch2BootTrace("06: system.Run() ok");

        if (launch_cfg.show_fps_mode > 0) {
            UpdateSwitch2FpsOverlay("0 FPS  0%");
        }

        // Move input polling loop off Core 0 so it does not compete with CPUCore_0.
        Common::SetCurrentThreadName("Switch2Input");
        Common::SetCurrentThreadPriority(Common::ThreadPriority::Low);

        Switch2PadSnapshot pad{};
        Switch2PadSnapshot prev_pad{};
        bool prev_touch = false;
        auto* active_ctrl = system->HIDCore().GetEmulatedController(active_npad_id);
        Core::HID::VibrationValue last_l{Core::HID::DEFAULT_VIBRATION_VALUE};
        Core::HID::VibrationValue last_r{Core::HID::DEFAULT_VIBRATION_VALUE};
        u32 loop_iters = 0;

        while (Switch2PollInput(&pad, true)) {
            ForwardSwitch2Input(*input_subsystem, pad, prev_pad, active_pad_index,
                                launch_cfg.controller_profile, prev_touch);
            if (active_ctrl != nullptr) {
                const auto vl = active_ctrl->GetActualVibrationValue(Core::HID::DeviceIndex::Left);
                const auto vr = active_ctrl->GetActualVibrationValue(Core::HID::DeviceIndex::Right);
                const bool diff_l =
                    vl.low_amplitude != last_l.low_amplitude ||
                    vl.high_amplitude != last_l.high_amplitude ||
                    ((vl.low_amplitude != 0.0f || vl.high_amplitude != 0.0f) &&
                     (vl.low_frequency != last_l.low_frequency ||
                      vl.high_frequency != last_l.high_frequency));
                const bool diff_r =
                    vr.low_amplitude != last_r.low_amplitude ||
                    vr.high_amplitude != last_r.high_amplitude ||
                    ((vr.low_amplitude != 0.0f || vr.high_amplitude != 0.0f) &&
                     (vr.low_frequency != last_r.low_frequency ||
                      vr.high_frequency != last_r.high_frequency));
                if (diff_l || diff_r) {
                    Switch2SetVibration(vl.low_amplitude, vl.low_frequency, vl.high_amplitude,
                                        vl.high_frequency, vr.low_amplitude, vr.low_frequency,
                                        vr.high_amplitude, vr.high_frequency);
                    last_l = vl;
                    last_r = vr;
                }
            }
            if (launch_cfg.show_fps_mode > 0 && (++loop_iters % 100u) == 0u) {
                const auto perf = system->GetAndResetPerfStats();
                char line[128];
                const double out_fps =
                    (launch_cfg.frame_gen_mult >= 2)
                        ? (perf.average_game_fps * static_cast<double>(launch_cfg.frame_gen_mult))
                        : perf.average_game_fps;
                if (launch_cfg.show_fps_mode == 2) {
                    const unsigned long long j_cnt =
                        g_switch2_stat_jit_blocks.exchange(0, std::memory_order_relaxed);
                    const unsigned long long i_cnt =
                        g_switch2_stat_icache_inv.exchange(0, std::memory_order_relaxed);
                    const unsigned long long e_cnt =
                        g_switch2_stat_excl_cb.exchange(0, std::memory_order_relaxed);
                    const unsigned long long s_cnt =
                        g_switch2_stat_svcs.exchange(0, std::memory_order_relaxed);
                    const char* mode_str =
                        (Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked)
                            ? "DOCKED"
                            : "HANDHELD";
                    if (launch_cfg.frame_gen_mult >= 2) {
                        std::snprintf(line, sizeof(line),
                                      "%.0f->%.0f FPS  %.0f%%\n%s %.2gX  LSFG %dX\nJ:%llu I:%llu E:%llu S:%llu",
                                      perf.average_game_fps, out_fps, perf.emulation_speed * 100.0,
                                      mode_str, launch_cfg.resolution_scale,
                                      launch_cfg.frame_gen_mult, j_cnt, i_cnt, e_cnt, s_cnt);
                    } else {
                        std::snprintf(line, sizeof(line),
                                      "%.0f FPS  %.0f%%\n%s %.2gX\nJ:%llu I:%llu E:%llu S:%llu",
                                      perf.average_game_fps, perf.emulation_speed * 100.0,
                                      mode_str, launch_cfg.resolution_scale, j_cnt, i_cnt, e_cnt,
                                      s_cnt);
                    }
                } else {
                    if (launch_cfg.frame_gen_mult >= 2) {
                        std::snprintf(line, sizeof(line), "%.0f->%.0f FPS  %.0f%%",
                                      perf.average_game_fps, out_fps, perf.emulation_speed * 100.0);
                    } else {
                        std::snprintf(line, sizeof(line), "%.0f FPS  %.0f%%",
                                      perf.average_game_fps, perf.emulation_speed * 100.0);
                    }
                }
                UpdateSwitch2FpsOverlay(line);
            }
            Switch2SleepMs(8);
        }
        // If Horizon didn't request exit (e.g. Home/Close), the user held [+]+[-] -> relaunch UI.
        should_return_to_launcher = Switch2MainLoop();
    } else {
        // Return cleanly to the launcher when a title fails to load (e.g. unsupported Switch 2 package).
        should_return_to_launcher = Switch2MainLoop();
    }

    Switch2BootTrace("99: shutdown");
    UpdateSwitch2FpsOverlay("");
    Switch2SetCpuBoost(false);
    Switch2SetVibration(0, 160.0f, 0, 320.0f, 0, 160.0f, 0, 320.0f);
    system->DetachDebugger();
    if (system->IsPoweredOn()) {
        system->Pause();
        system->ShutdownMainProcess();
    }
    system->HIDCore().UnloadInputDevices();
    emu_window.reset();
    system.reset();
    manual_provider.reset();
    input_subsystem->Shutdown();
    input_subsystem.reset();
    Common::Log::Stop();
    Dynarmic::Backend::Arm64::Switch2JitPool::Shutdown();
    if (should_return_to_launcher) {
        Switch2ScheduleReturnToLauncher(argc > 0 ? argv[0] : nullptr);
    }
    return 0;
}

#else

struct SdlState {
    Core::System system{};
    InputCommon::InputSubsystem input_subsystem{};
    std::unique_ptr<EmuWindow_SDL3> emu_window;
    bool system_initialized = false;
};

extern "C" SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
#ifdef __SWITCH2__
    std::set_terminate([]() {
        std::error_code ec;
        std::filesystem::create_directories("sdmc:/switch/eden-nx2", ec);
        if (FILE* f = std::fopen("sdmc:/switch/eden-nx2/crash.txt", "a")) {
            if (auto ex = std::current_exception()) {
                try {
                    std::rethrow_exception(ex);
                } catch (const std::exception& e) {
                    std::fprintf(f, "std::terminate: %s\n", e.what());
                } catch (...) {
                    std::fprintf(f, "std::terminate: unknown exception\n");
                }
            } else {
                std::fprintf(f, "std::terminate: no active exception\n");
            }
            std::fflush(f);
            std::fclose(f);
        }
        std::abort();
    });
    if (FILE* f = std::fopen("sdmc:/switch/eden-nx2/boot_trace.txt", "w")) {
        std::fprintf(f, "00: SDL_AppInit enter (argc=%d)\n", argc);
        std::fflush(f);
        std::fclose(f);
    } else {
        Switch2BootTrace("00: SDL_AppInit enter");
    }
#endif
    SdlState* state = new SdlState();
    *appstate = state;
#ifdef __SWITCH2__
    Switch2BootTrace("01: SdlState allocated");
#endif

#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "wb", stdout);
        freopen("CONOUT$", "wb", stderr);
    }
#endif

    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(true);
    Common::Log::Start();
#ifdef __SWITCH2__
    Switch2BootTrace("02: Log started");
#endif

    int option_index = 0;
#ifdef _WIN32
    int argc_w;
    auto argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    if (argv_w == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to get command line arguments");
        return SDL_APP_FAILURE;
    }
#else
    wchar_t **argv_w = nullptr;
#endif
    Core::LaunchParams lp = Core::ParseLaunchParams(state->system, argc, argv, argv_w);
#ifdef _WIN32
    LocalFree(argv_w);
#endif
#ifdef __SWITCH2__
    Switch2BootTrace("03: ParseLaunchParams ok");
#endif
    SdlConfig config{lp.config_path};
#ifdef __SWITCH2__
    Switch2BootTrace("04: SdlConfig ok");
#endif

    Core::ApplyLaunchParams(lp);
#ifdef __SWITCH2__
    Switch2BootTrace("05: ApplyLaunchParams ok");
#endif

#ifdef __SWITCH2__
    {
        std::error_code ec;
        std::filesystem::create_directories("sdmc:/switch/eden-nx2/roms", ec);
        Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
        Settings::values.cpu_backend.SetValue(Settings::CpuBackend::Dynarmic);
        // No SIGSEGV/fastmem handler on Switch 2: must use software page tables.
        Settings::values.cpuopt_fastmem.SetValue(false);
        Settings::values.log_flush_line.SetValue(true);
        auto& player1 = Settings::values.players.GetValue()[0];
        player1.connected = true;
        player1.controller_type = Settings::ControllerType::ProController;
        lp.fullscreen = true;
        if (lp.filepath.empty() && std::filesystem::exists("sdmc:/switch/eden-nx2/roms", ec)) {
            for (const auto& entry : std::filesystem::directory_iterator("sdmc:/switch/eden-nx2/roms", ec)) {
                if (!entry.is_regular_file(ec)) {
                    continue;
                }
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".nsp" || ext == ".xci" || ext == ".nro") {
                    lp.filepath = entry.path().string();
                    break;
                }
            }
        }
    }
#endif

    if (lp.print_help || lp.print_version) {
        return SDL_APP_SUCCESS;
    }

    if (lp.filepath.empty()) {
        LOG_CRITICAL(Frontend, "Failed to load ROM: No ROM specified (place a .nsp/.xci in sdmc:/switch/eden-nx2/roms)");
#ifdef __SWITCH2__
        Switch2BootTrace("06: No ROM found in sdmc:/switch/eden-nx2/roms - exiting cleanly");
        return SDL_APP_SUCCESS;
#else
        return SDL_APP_FAILURE;
#endif
    }

#ifdef __SWITCH2__
    Switch2BootTrace("06: ROM selected, calling system.Initialize()");
#endif
    state->system.Initialize();
    state->system_initialized = true;
#ifdef __SWITCH2__
    Switch2BootTrace("07: system.Initialize() ok");
#endif

    // Apply the command line arguments
    state->system.ApplySettings();

    switch (Settings::values.renderer_backend.GetValue()) {
#ifdef HAS_OPENGL
    case Settings::RendererBackend::OpenGL_GLSL:
    case Settings::RendererBackend::OpenGL_GLASM:
    case Settings::RendererBackend::OpenGL_SPIRV:
        state->emu_window = std::make_unique<EmuWindow_SDL3_GL>(&state->input_subsystem, state->system, lp.fullscreen);
        break;
#endif
    case Settings::RendererBackend::Vulkan:
        state->emu_window = std::make_unique<EmuWindow_SDL3_VK>(&state->input_subsystem, state->system, lp.fullscreen);
        break;
    case Settings::RendererBackend::Null:
        state->emu_window = std::make_unique<EmuWindow_SDL3_Null>(&state->input_subsystem, state->system, lp.fullscreen);
        break;
    default:
        LOG_CRITICAL(Frontend, "Invalid renderer backend");
        return SDL_APP_FAILURE;
    }
#ifdef __SWITCH2__
    Switch2BootTrace("08: EmuWindow_SDL3_VK ok");
#endif

#ifdef _WIN32
    Common::Windows::SetCurrentTimerResolutionToMaximum();
    state->system.CoreTiming().SetTimerResolutionNs(Common::Windows::GetCurrentTimerResolution());
#endif

    state->system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    state->system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    state->system.GetFileSystemController().CreateFactories(*state->system.GetFilesystem());
    state->system.GetUserChannel().clear();

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    const Core::SystemResultStatus load_result = state->system.Load(*state->emu_window, lp.filepath, load_parameters);
    switch (load_result) {
    case Core::SystemResultStatus::Success:
        break; // Expected case
    case Core::SystemResultStatus::ErrorGetLoader:
        LOG_CRITICAL(Frontend, "Failed to obtain loader for {}!", lp.filepath);
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorLoader:
        LOG_CRITICAL(Frontend, "Failed to load ROM!");
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorNotInitialized:
        LOG_CRITICAL(Frontend, "CPUCore not initialized");
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorVideoCore:
        LOG_CRITICAL(Frontend, "Failed to initialize VideoCore!");
        return SDL_APP_FAILURE;
    default:
        const u16 loader_id = u16(Core::SystemResultStatus::ErrorLoader);
        const u16 error_id = u16(load_result) - loader_id;
        LOG_CRITICAL(Frontend,
            "While attempting to load the ROM requested, an error occurred. Please "
            "refer to the Eden wiki for more information or the Eden discord for "
            "additional help.\n\nError Code: {:04X}-{:04X}\nError Description: {}",
            loader_id, error_id, Loader::GetResultStatusString(Loader::ResultStatus(error_id)));
        return SDL_APP_FAILURE;
    }
#ifdef __SWITCH2__
    Switch2BootTrace("09: system.Load() ok");
#endif

    if (lp.use_multiplayer) {
        if (auto member = Network::GetRoomMember().lock()) {
            member->BindOnChatMessageReceived(OnMessageReceived);
            member->BindOnStatusMessageReceived(OnStatusMessageReceived);
            member->BindOnStateChanged(OnStateChanged);
            member->BindOnError(OnNetworkError);
            LOG_DEBUG(Network, "Start connection to {}:{} with nickname {}", lp.address, lp.port, lp.nickname);
            member->Join(lp.nickname, lp.address.c_str(), lp.port, 0, Network::NoPreferredIP, lp.password);
        } else {
            LOG_ERROR(Network, "Could not access RoomMember");
            return SDL_APP_FAILURE;
        }
    }

    // Core is loaded, start the GPU (makes the GPU contexts current to this thread)
    state->system.GPU().Start();
    state->system.GetCpuManager().OnGpuReady();
#ifdef __SWITCH2__
    Switch2BootTrace("10: GPU.Start() ok");
#endif

    if (Settings::values.use_disk_shader_cache.GetValue()) {
        state->system.Renderer().ReadRasterizer()->LoadDiskResources(
            state->system.GetApplicationProcessProgramID(), std::stop_token{},
            [](VideoCore::LoadCallbackStage, size_t value, size_t total) {});
    }

    // don't do anything, SDL3 already exists for us :D
    state->system.RegisterExitCallback([] {});

    // QLaunch launched applications (should) now reload shader cache.
    static std::mutex shader_cache_reload_mutex;
    state->system.RegisterApplicationChangedCallback([state](u64 changed_program_id) {
        if (!Settings::values.use_disk_shader_cache.GetValue()) {
            return;
        }

        std::scoped_lock lk{shader_cache_reload_mutex};

        LOG_INFO(Frontend, "Reloading disk shader cache for {:016X}", changed_program_id);

        state->system.Pause();
        state->system.GPU().WaitForIdle();
        state->system.GPU().ObtainContext();

        state->system.Renderer().ReadRasterizer()->LoadDiskResources(
            changed_program_id, std::stop_token{},
            [](VideoCore::LoadCallbackStage, size_t, size_t) {});

        state->system.GPU().ReleaseContext();
        state->system.Run();
    });

    void(state->system.Run());
#ifdef __SWITCH2__
    Switch2BootTrace("11: system.Run() ok");
#endif
    if (state->system.DebuggerEnabled())
        state->system.InitializeDebugger();
    return SDL_APP_CONTINUE;
}
extern "C" SDL_AppResult SDL_AppIterate(void *appstate) {
#ifdef __SWITCH2__
    if (!appletMainLoop()) {
        return SDL_APP_SUCCESS;
    }
#endif
    SdlState *state = (SdlState *)appstate;
    return state->emu_window->IsOpen() ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}
extern "C" SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    SdlState *state = (SdlState *)appstate;
    state->emu_window->OnEvent(*event);
    return SDL_APP_CONTINUE;
}
extern "C" void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    SdlState *state = (SdlState *)appstate;
    if (state == nullptr) {
        return;
    }
#ifdef __SWITCH2__
    Switch2BootTrace("99: SDL_AppQuit");
#endif
    if (state->system_initialized) {
        state->system.DetachDebugger();
        if (state->system.IsPoweredOn()) {
            state->system.Pause();
            state->system.ShutdownMainProcess();
        }
    }
    delete state;
}

#endif

#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"

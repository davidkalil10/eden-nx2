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
#include <cstring>
#include <exception>
#include <filesystem>
#include <tuple>
#include "core/frontend/applets/software_keyboard.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/am/frontend/applets.h"
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

struct Switch2KeyboardBridge {
    std::mutex mutex;
    bool pending = false;
    bool close_requested = false;
    bool is_inline = false;
    Core::Frontend::KeyboardInitializeParameters params{};
    Core::Frontend::InlineAppearParameters inline_params{};
    std::u16string inline_text{};
    Core::Frontend::SoftwareKeyboardApplet::SubmitNormalCallback submit_normal;
    Core::Frontend::SoftwareKeyboardApplet::SubmitInlineCallback submit_inline;
};

class Switch2SoftwareKeyboardApplet final : public Core::Frontend::SoftwareKeyboardApplet {
public:
    explicit Switch2SoftwareKeyboardApplet(std::shared_ptr<Switch2KeyboardBridge> bridge_)
        : bridge(std::move(bridge_)) {}

    ~Switch2SoftwareKeyboardApplet() override = default;

    void Close() const override {
        std::scoped_lock lk{bridge->mutex};
        bridge->pending = false;
        bridge->close_requested = true;
    }

    void InitializeKeyboard(bool is_inline,
                            Core::Frontend::KeyboardInitializeParameters initialize_parameters,
                            SubmitNormalCallback submit_normal_callback_,
                            SubmitInlineCallback submit_inline_callback_) override {
        std::scoped_lock lk{bridge->mutex};
        params = std::move(initialize_parameters);
        bridge->inline_text = params.initial_text;
        if (is_inline) {
            submit_inline_cb = std::move(submit_inline_callback_);
        } else {
            submit_normal_cb = std::move(submit_normal_callback_);
        }
    }

    void ShowNormalKeyboard() const override {
        std::scoped_lock lk{bridge->mutex};
        bridge->is_inline = false;
        bridge->params = params;
        bridge->submit_normal = submit_normal_cb;
        bridge->submit_inline = nullptr;
        bridge->close_requested = false;
        bridge->pending = true;
        LOG_INFO(Frontend, "Switch2 swkbd requested (normal, max_len={})", params.max_text_length);
    }

    void ShowTextCheckDialog(Service::AM::Frontend::SwkbdTextCheckResult text_check_result,
                             std::u16string text_check_message) const override {
        LOG_WARNING(Frontend, "Switch2 swkbd text check dialog ignored (result={}, msg={})",
                    static_cast<u32>(text_check_result), Common::UTF16ToUTF8(text_check_message));
    }

    void ShowInlineKeyboard(Core::Frontend::InlineAppearParameters appear_parameters) const override {
        std::scoped_lock lk{bridge->mutex};
        bridge->is_inline = true;
        bridge->params = params;
        bridge->inline_params = appear_parameters;
        bridge->submit_normal = nullptr;
        bridge->submit_inline = submit_inline_cb;
        bridge->close_requested = false;
        bridge->pending = true;
        LOG_INFO(Frontend, "Switch2 swkbd requested (inline, max_len={})",
                 appear_parameters.max_text_length);
    }

    void HideInlineKeyboard() const override {
        std::scoped_lock lk{bridge->mutex};
        bridge->pending = false;
        bridge->close_requested = true;
    }

    void InlineTextChanged(Core::Frontend::InlineTextParameters text_parameters) const override {
        SubmitInlineCallback cb;
        {
            std::scoped_lock lk{bridge->mutex};
            bridge->inline_text = text_parameters.input_text;
            cb = submit_inline_cb;
        }
        if (cb) {
            cb(Service::AM::Frontend::SwkbdReplyType::ChangedString, text_parameters.input_text,
               text_parameters.cursor_position);
        }
    }

    void ExitKeyboard() const override {
        std::scoped_lock lk{bridge->mutex};
        bridge->pending = false;
        bridge->close_requested = true;
    }

private:
    std::shared_ptr<Switch2KeyboardBridge> bridge;
    Core::Frontend::KeyboardInitializeParameters params{};
    mutable SubmitNormalCallback submit_normal_cb;
    mutable SubmitInlineCallback submit_inline_cb;
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

bool IsSwitch2SplitDirPath(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p / "00", ec) && !ec;
}

bool IsSwitch2AddonContainerName(const std::string& lower_name) {
    auto ends_with = [&](std::string_view suf) {
        return lower_name.size() >= suf.size() &&
               lower_name.compare(lower_name.size() - suf.size(), suf.size(), suf) == 0;
    };
    if (ends_with(".nsp") || ends_with(".nsz") || ends_with(".xci") || ends_with(".xcz")) {
        return true;
    }
    if (ends_with(".nsp.00") || ends_with(".nsz.00") || ends_with(".xci.00") ||
        ends_with(".xcz.00") || ends_with(".nsp.001") || ends_with(".nsz.001") ||
        ends_with(".xci.001") || ends_with(".xcz.001") || ends_with(".xc0") ||
        ends_with(".ns0")) {
        return true;
    }
    return false;
}

void ScanSwitch2AddonDir(FileSys::ManualContentProvider& provider,
                         const FileSys::VirtualFilesystem& vfs, const std::string& dir,
                         const std::string& skip_path, bool filter_upd, bool filter_dlc,
                         bool recurse_one) {
    try {
        std::error_code ec;
        if (dir.empty() || !std::filesystem::is_directory(dir, ec) || ec) return;
        std::filesystem::directory_iterator it(dir, ec);
        const std::filesystem::directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const auto& entry = *it;
            const std::string p = entry.path().generic_string();
            if (p == skip_path) continue;
            const std::string lower_name = Common::ToLower(entry.path().filename().string());
            auto passes_filters = [&]() {
                if (filter_upd &&
                    lower_name.find("upd") == std::string::npos &&
                    lower_name.find("patch") == std::string::npos &&
                    lower_name.find("[v") == std::string::npos) {
                    return false;
                }
                if (filter_dlc &&
                    lower_name.find("dlc") == std::string::npos &&
                    lower_name.find("aoc") == std::string::npos) {
                    return false;
                }
                return true;
            };
            if (entry.is_directory(ec)) {
                if (IsSwitch2SplitDirPath(entry.path())) {
                    if (passes_filters()) {
                        if (auto f = Core::GetGameFileFromPath(vfs, p)) {
                            provider.AddEntriesFromContainer(f, false);
                        }
                    }
                    continue;
                }
                if (recurse_one) {
                    const std::string dname = entry.path().filename().string();
                    if (!dname.empty() && dname[0] != '.') {
                        ScanSwitch2AddonDir(provider, vfs, p, skip_path, filter_upd, filter_dlc,
                                            false);
                    }
                }
                continue;
            }
            if (!entry.is_regular_file(ec)) continue;
            if (!IsSwitch2AddonContainerName(lower_name)) continue;
            if (!passes_filters()) continue;
            if (auto f = Core::GetGameFileFromPath(vfs, p)) {
                provider.AddEntriesFromContainer(f, false);
            }
        }
    } catch (...) {
    }
}

void RegisterSwitch2Addons(FileSys::ManualContentProvider& provider,
                           const FileSys::VirtualFilesystem& vfs, const Switch2LaunchConfig& cfg) {
    auto add_file = [&](const std::string& path) {
        std::error_code ec;
        if (path.empty()) return;
        if (!std::filesystem::is_regular_file(path, ec) &&
            !IsSwitch2SplitDirPath(std::filesystem::path(path))) {
            return;
        }
        if (auto f = Core::GetGameFileFromPath(vfs, path)) {
            provider.AddEntriesFromContainer(f, false);
        }
    };

    const std::string rom_dir = std::filesystem::path(cfg.rom_path).parent_path().generic_string();

    // Register the launched base game container itself into ManualContentProvider so Control NCA
    // (NACP metadata, supported_languages, seed_for_pseudo_device_id) is always available even
    // when Updates are disabled. Skip compressed .nsz/.xcz to avoid double NCZ decompression.
    {
        const std::string lower_rom = Common::ToLower(cfg.rom_path);
        const bool is_compressed = lower_rom.ends_with(".nsz") || lower_rom.ends_with(".xcz") ||
                                   lower_rom.find(".nsz.") != std::string::npos ||
                                   lower_rom.find(".xcz.") != std::string::npos;
        if (!is_compressed) {
            add_file(cfg.rom_path);
        }
    }

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
                const std::filesystem::path item_p(item);
                if (IsSwitch2SplitDirPath(item_p)) {
                    add_file(item);
                } else if (std::filesystem::is_directory(item_p, ec)) {
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

struct Switch2PlayerBinding {
    bool enabled = false;
    std::size_t pad_index = 0;
    Core::HID::NpadIdType npad_id = Core::HID::NpadIdType::Player1;
    Switch2ControllerProfile effective_profile = Switch2ControllerProfile::ProController;
    std::uint8_t last_physical_style = SWITCH2_STYLE_NONE;
};

Settings::ControllerType Switch2ProfileToEdenType(Switch2ControllerProfile profile) {
    switch (profile) {
    case Switch2ControllerProfile::Handheld:
        return Settings::ControllerType::Handheld;
    case Switch2ControllerProfile::DualJoycon:
        return Settings::ControllerType::DualJoyconDetached;
    case Switch2ControllerProfile::RightJoycon:
        return Settings::ControllerType::RightJoycon;
    case Switch2ControllerProfile::LeftJoycon:
        return Settings::ControllerType::LeftJoycon;
    default:
        return Settings::ControllerType::ProController;
    }
}

Switch2ControllerProfile ResolveSwitch2P1Profile(Switch2ControllerProfile cfg_p1,
                                                 std::uint8_t phys_style) {
    if (cfg_p1 != Switch2ControllerProfile::Auto) {
        return cfg_p1;
    }
    switch (phys_style) {
    case SWITCH2_STYLE_HANDHELD:
        return Switch2ControllerProfile::Handheld;
    case SWITCH2_STYLE_JOY_DUAL:
        return Switch2ControllerProfile::DualJoycon;
    case SWITCH2_STYLE_JOY_LEFT:
        return Switch2ControllerProfile::LeftJoycon;
    case SWITCH2_STYLE_JOY_RIGHT:
        return Switch2ControllerProfile::RightJoycon;
    default:
        return Switch2ControllerProfile::ProController;
    }
}

bool ResolveSwitch2ExtraPlayer(Switch2PlayerSlotMode mode, bool phys_connected,
                               std::uint8_t phys_style, Switch2ControllerProfile* out_profile) {
    if (mode == Switch2PlayerSlotMode::Off) {
        return false;
    }
    if (mode == Switch2PlayerSlotMode::Auto) {
        if (!phys_connected) {
            return false;
        }
        switch (phys_style) {
        case SWITCH2_STYLE_JOY_LEFT:
            *out_profile = Switch2ControllerProfile::LeftJoycon;
            break;
        case SWITCH2_STYLE_JOY_RIGHT:
            *out_profile = Switch2ControllerProfile::RightJoycon;
            break;
        case SWITCH2_STYLE_JOY_DUAL:
            *out_profile = Switch2ControllerProfile::DualJoycon;
            break;
        default:
            *out_profile = Switch2ControllerProfile::ProController;
            break;
        }
        return true;
    }
    switch (mode) {
    case Switch2PlayerSlotMode::DualJoycon:
        *out_profile = Switch2ControllerProfile::DualJoycon;
        break;
    case Switch2PlayerSlotMode::RightJoycon:
        *out_profile = Switch2ControllerProfile::RightJoycon;
        break;
    case Switch2PlayerSlotMode::LeftJoycon:
        *out_profile = Switch2ControllerProfile::LeftJoycon;
        break;
    default:
        *out_profile = Switch2ControllerProfile::ProController;
        break;
    }
    return true;
}

void ConfigureSwitch2Controllers(const Switch2LaunchConfig& cfg,
                                 const std::array<Switch2PadSnapshot, 4>& pads,
                                 std::array<Switch2PlayerBinding, 4>& bindings,
                                 bool apply_console_mode) {
    auto& players = Settings::values.players.GetValue();
    for (auto& p : players) {
        p.connected = false;
    }
    bindings = {};

    static constexpr std::array<Core::HID::NpadIdType, 4> kPlayerNpads{{
        Core::HID::NpadIdType::Player1,
        Core::HID::NpadIdType::Player2,
        Core::HID::NpadIdType::Player3,
        Core::HID::NpadIdType::Player4,
    }};

    // Player 1
    const Switch2ControllerProfile p1_eff =
        ResolveSwitch2P1Profile(cfg.controller_profile, pads[0].physical_style);
    bindings[0].enabled = true;
    bindings[0].effective_profile = p1_eff;
    bindings[0].last_physical_style = pads[0].physical_style;
    if (p1_eff == Switch2ControllerProfile::Handheld) {
        if (apply_console_mode) {
            Settings::values.use_docked_mode.SetValue(Settings::ConsoleMode::Handheld);
        }
        players[8].connected = true;
        players[8].controller_type = Settings::ControllerType::Handheld;
        bindings[0].pad_index = 8;
        bindings[0].npad_id = Core::HID::NpadIdType::Handheld;
    } else {
        if (apply_console_mode) {
            Settings::values.use_docked_mode.SetValue(cfg.docked_mode
                                                          ? Settings::ConsoleMode::Docked
                                                          : Settings::ConsoleMode::Handheld);
        }
        players[0].connected = true;
        players[0].controller_type = Switch2ProfileToEdenType(p1_eff);
        bindings[0].pad_index = 0;
        bindings[0].npad_id = Core::HID::NpadIdType::Player1;
    }

    // Players 2..4
    for (int p = 1; p < 4; ++p) {
        const Switch2PlayerSlotMode slot_mode = (p == 1) ? cfg.controller_p2 : cfg.controller_p34;
        bindings[static_cast<std::size_t>(p)].last_physical_style =
            pads[static_cast<std::size_t>(p)].physical_style;
        Switch2ControllerProfile eff = Switch2ControllerProfile::ProController;
        if (ResolveSwitch2ExtraPlayer(slot_mode, pads[static_cast<std::size_t>(p)].connected,
                                      pads[static_cast<std::size_t>(p)].physical_style, &eff)) {
            players[static_cast<std::size_t>(p)].connected = true;
            players[static_cast<std::size_t>(p)].controller_type = Switch2ProfileToEdenType(eff);
            bindings[static_cast<std::size_t>(p)].enabled = true;
            bindings[static_cast<std::size_t>(p)].pad_index = static_cast<std::size_t>(p);
            bindings[static_cast<std::size_t>(p)].npad_id = kPlayerNpads[static_cast<std::size_t>(p)];
            bindings[static_cast<std::size_t>(p)].effective_profile = eff;
        }
    }
}

void ApplySwitch2LaunchConfig(const Switch2LaunchConfig& cfg,
                              const std::array<Switch2PadSnapshot, 4>& pads,
                              std::array<Switch2PlayerBinding, 4>& bindings) {
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

    ConfigureSwitch2Controllers(cfg, pads, bindings, true);
    Settings::UpdateRescalingInfo();
}

void ApplySwitch2LiveSettings(Core::System& system, const Switch2LaunchConfig& cfg,
                              int volume_pct, bool docked_changed) {
    if (cfg.speed_limit <= 0) {
        Settings::values.use_speed_limit.SetValue(false);
    } else {
        Settings::values.use_speed_limit.SetValue(true);
        Settings::values.speed_limit.SetValue(static_cast<u16>(cfg.speed_limit));
    }

    const Settings::VSyncMode vsync =
        (cfg.vsync_mode == 1)
            ? Settings::VSyncMode::Immediate
            : (cfg.vsync_mode == 2
                   ? Settings::VSyncMode::Mailbox
                   : (cfg.vsync_mode == 3 ? Settings::VSyncMode::Fifo
                                          : Settings::VSyncMode::FifoRelaxed));
    Settings::values.vsync_mode.SetValue(vsync);

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

    Settings::values.use_docked_mode.SetValue(cfg.docked_mode ? Settings::ConsoleMode::Docked
                                                              : Settings::ConsoleMode::Handheld);
    if (docked_changed) {
        system.GetAppletManager().OperationModeChanged();
    }

    if (cfg.gpu_fast) {
        Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::Low);
        Settings::values.use_reactive_flushing.SetValue(false);
        Settings::values.dma_accuracy.SetValue(Settings::DmaAccuracy::Unsafe);
        Settings::values.barrier_feedback_loops.SetValue(false);
        Settings::values.skip_cpu_inner_invalidation.SetValue(true);
    } else {
        Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::High);
        Settings::values.use_reactive_flushing.SetValue(true);
        Settings::values.dma_accuracy.SetValue(Settings::DmaAccuracy::Safe);
        Settings::values.barrier_feedback_loops.SetValue(true);
        Settings::values.skip_cpu_inner_invalidation.SetValue(false);
    }
    Settings::UpdateGPUAccuracy();

    Switch2SetCpuBoost(cfg.cpu_boost);
    Settings::values.volume.SetValue(static_cast<u8>(std::clamp(volume_pct, 0, 150)));
    if (cfg.show_fps_mode == 0) {
        UpdateSwitch2FpsOverlay("");
    }
}

void ForwardSwitch2Input(InputCommon::InputSubsystem& input_subsystem,
                         const Switch2PadSnapshot& pad, Switch2PadSnapshot& prev,
                         std::size_t pad_index, Switch2ControllerProfile profile,
                         bool forward_touch, bool& prev_touch) {
    auto* gamepad = input_subsystem.GetVirtualGamepad();
    if (gamepad != nullptr) {
        const std::uint32_t changed = pad.buttons ^ prev.buttons;
        for (int button = 0; button < 16; ++button) {
            if (changed & (1u << button)) {
                gamepad->SetButtonState(pad_index, button, (pad.buttons & (1u << button)) != 0);
            }
        }

        // SL/SR mapping: real single Joy-Cons send native SL/SR (bits 16/17);
        // Pro/Handheld emulating a single Joy-Con also alias ZL/ZR to SL/SR for comfort.
        const bool is_real_single_jc = (pad.physical_style == SWITCH2_STYLE_JOY_LEFT ||
                                        pad.physical_style == SWITCH2_STYLE_JOY_RIGHT);
        const bool single_jc_alias =
            !is_real_single_jc && (profile == Switch2ControllerProfile::RightJoycon ||
                                   profile == Switch2ControllerProfile::LeftJoycon);
        const auto calc_sl = [&](std::uint32_t btns) {
            return (btns & (1u << 16)) != 0 || (single_jc_alias && (btns & (1u << 8)) != 0);
        };
        const auto calc_sr = [&](std::uint32_t btns) {
            return (btns & (1u << 17)) != 0 || (single_jc_alias && (btns & (1u << 9)) != 0);
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

        // Sticks (when Pro/Handheld emulates RightJoycon, mirror left stick to right stick if idle)
        float rx = pad.right_x;
        float ry = pad.right_y;
        const bool mirror_right = !is_real_single_jc &&
                                  profile == Switch2ControllerProfile::RightJoycon;
        if (mirror_right && std::fabs(rx) < 0.12f && std::fabs(ry) < 0.12f) {
            rx = pad.left_x;
            ry = pad.left_y;
        }
        if (pad.left_x != prev.left_x || pad.left_y != prev.left_y) {
            gamepad->SetStickPosition(pad_index, InputCommon::VirtualGamepad::VirtualStick::Left,
                                      pad.left_x, pad.left_y);
        }
        if (rx != prev.right_x || ry != prev.right_y ||
            (mirror_right && (pad.left_x != prev.left_x || pad.left_y != prev.left_y))) {
            gamepad->SetStickPosition(pad_index, InputCommon::VirtualGamepad::VirtualStick::Right,
                                      rx, ry);
        }

        if (pad.has_motion) {
            gamepad->SetMotionState(pad_index, pad.motion_delta_us, pad.gyro[0], pad.gyro[1],
                                    pad.gyro[2], pad.accel[0], pad.accel[1], pad.accel[2]);
        }
    }

    if (forward_touch) {
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
        std::filesystem::remove("sdmc:/switch/eden-nx2/boot_trace_prev.txt", ec);
        ec.clear();
        std::filesystem::rename("sdmc:/switch/eden-nx2/boot_trace.txt",
                                "sdmc:/switch/eden-nx2/boot_trace_prev.txt", ec);
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
        std::error_code ec;
        std::filesystem::create_directories("sdmc:/switch/eden-nx2/.local/share/eden/play_time", ec);
        std::filesystem::remove("sdmc:/switch/eden-nx2/last_guest_panic.txt", ec);
        std::string msg = "02: ROM=" + launch_cfg.rom_path;
        Switch2BootTrace(msg.c_str());
    }

    std::array<Switch2PadSnapshot, 4> init_pads{};
    for (int p = 0; p < 4; ++p) {
        Switch2PollPlayerInput(p, &init_pads[static_cast<std::size_t>(p)]);
    }
    std::array<Switch2PlayerBinding, 4> player_bindings{};
    ApplySwitch2LaunchConfig(launch_cfg, init_pads, player_bindings);

    auto manual_provider = std::make_unique<FileSys::ManualContentProvider>();
    auto system = std::make_unique<Core::System>();
    auto kbd_bridge = std::make_shared<Switch2KeyboardBridge>();
    {
        Service::AM::Frontend::FrontendAppletSet applets{};
        applets.software_keyboard = std::make_unique<Switch2SoftwareKeyboardApplet>(kbd_bridge);
        system->SetFrontendAppletSet(std::move(applets));
    }
    auto input_subsystem = std::make_unique<InputCommon::InputSubsystem>();
    auto emu_window = std::make_unique<EmuWindowSwitch2>();
    Switch2BootTrace("03: System/Window created");

    system->Initialize();
    if (!launch_cfg.profile_name.empty()) {
        auto& pm = system->GetProfileManager();
        if (const auto uid = pm.GetUser(0); uid.has_value()) {
            Service::Account::ProfileBase base{};
            if (pm.GetProfileBase(*uid, base)) {
                Service::Account::ProfileUsername new_name{};
                const std::size_t n = std::min(launch_cfg.profile_name.size(),
                                               Service::Account::profile_username_size - 1);
                std::memcpy(new_name.data(), launch_cfg.profile_name.data(), n);
                if (base.username != new_name) {
                    base.username = new_name;
                    pm.SetProfileBase(*uid, base);
                }
            }
        }
    }
    system->ApplySettings();
    input_subsystem->Initialize();
    system->HIDCore().ReloadInputDevices();
    auto content_provider = std::make_unique<FileSys::ContentProviderUnion>();
    content_provider->SetSlot(FileSys::ContentProviderUnionSlot::FrontendManual,
                              manual_provider.get());
    system->SetContentProvider(std::move(content_provider));
    system->SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    system->GetFileSystemController().CreateFactories(*system->GetFilesystem());
    RegisterSwitch2Addons(*manual_provider, system->GetFilesystem(), launch_cfg);
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

        std::array<Switch2PadSnapshot, 4> pads{};
        std::array<Switch2PadSnapshot, 4> prev_pads{};
        bool prev_touch = false;
        std::array<Core::HID::EmulatedController*, 4> active_ctrls{};
        std::array<Core::HID::VibrationValue, 4> last_l{};
        std::array<Core::HID::VibrationValue, 4> last_r{};
        for (int p = 0; p < 4; ++p) {
            last_l[static_cast<std::size_t>(p)] = Core::HID::DEFAULT_VIBRATION_VALUE;
            last_r[static_cast<std::size_t>(p)] = Core::HID::DEFAULT_VIBRATION_VALUE;
        }
        auto refresh_emulated_ctrls = [&]() {
            for (int p = 0; p < 4; ++p) {
                const auto& b = player_bindings[static_cast<std::size_t>(p)];
                active_ctrls[static_cast<std::size_t>(p)] =
                    b.enabled ? system->HIDCore().GetEmulatedController(b.npad_id) : nullptr;
            }
        };
        auto stop_all_vibration = [&]() {
            for (int p = 0; p < 4; ++p) {
                Switch2SetPlayerVibration(p, 0, 160.0f, 0, 320.0f, 0, 160.0f, 0, 320.0f);
                last_l[static_cast<std::size_t>(p)] = Core::HID::DEFAULT_VIBRATION_VALUE;
                last_r[static_cast<std::size_t>(p)] = Core::HID::DEFAULT_VIBRATION_VALUE;
            }
        };
        refresh_emulated_ctrls();
        u32 loop_iters = 0;

        launch_cfg.docked_mode =
            (Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked);
        bool quick_menu_open = false;
        int quick_sel = 0;
        int volume_pct = static_cast<int>(Settings::values.volume.GetValue());
        std::string quick_msg;
        bool exit_to_launcher = false;

        bool kbd_overlay_active = false;
        bool kbd_is_inline = false;
        bool kbd_shift = true;
        bool kbd_symbols = false;
        bool kbd_prev_touch = false;
        int kbd_row = 0;
        int kbd_col = 0;
        int kbd_max_len = 32;
        int kbd_min_len = 0;
        std::string kbd_header;
        std::string kbd_text;
        Core::Frontend::SoftwareKeyboardApplet::SubmitNormalCallback kbd_norm_cb;
        Core::Frontend::SoftwareKeyboardApplet::SubmitInlineCallback kbd_inline_cb;

        auto submit_kbd_result = [&](bool ok, const std::string& utf8_text) {
            const std::u16string u16 = Common::UTF8ToUTF16(utf8_text);
            if (kbd_is_inline) {
                if (kbd_inline_cb) {
                    if (ok) {
                        const s32 pos = static_cast<s32>(u16.size());
                        kbd_inline_cb(Service::AM::Frontend::SwkbdReplyType::ChangedString, u16, pos);
                        kbd_inline_cb(Service::AM::Frontend::SwkbdReplyType::DecidedEnter, u16, pos);
                    } else {
                        kbd_inline_cb(Service::AM::Frontend::SwkbdReplyType::DecidedCancel, u"", 0);
                    }
                }
            } else {
                if (kbd_norm_cb) {
                    if (ok) {
                        kbd_norm_cb(Service::AM::Frontend::SwkbdResult::Ok, u16, true);
                    } else {
                        kbd_norm_cb(Service::AM::Frontend::SwkbdResult::Cancel, u"", true);
                    }
                }
            }
            kbd_norm_cb = nullptr;
            kbd_inline_cb = nullptr;
            kbd_overlay_active = false;
            Switch2HideKeyboardOverlay();
        };

        auto neutralize_all_inputs = [&]() {
            Switch2PadSnapshot neutral{};
            for (int p = 0; p < 4; ++p) {
                const auto& b = player_bindings[static_cast<std::size_t>(p)];
                if (b.enabled) {
                    ForwardSwitch2Input(*input_subsystem, neutral,
                                        prev_pads[static_cast<std::size_t>(p)], b.pad_index,
                                        b.effective_profile, p == 0, prev_touch);
                }
            }
            stop_all_vibration();
        };

        while (Switch2PollInput(&pads[0], true)) {
            for (int p = 1; p < 4; ++p) {
                Switch2PollPlayerInput(p, &pads[static_cast<std::size_t>(p)]);
            }

            // Detect hot-plug or Joy-Con detach/reattach for any Auto-configured player slot.
            bool need_ctrl_reload = false;
            if (launch_cfg.controller_profile == Switch2ControllerProfile::Auto &&
                pads[0].physical_style != player_bindings[0].last_physical_style) {
                need_ctrl_reload = true;
            }
            for (int p = 1; p < 4; ++p) {
                const Switch2PlayerSlotMode sm =
                    (p == 1) ? launch_cfg.controller_p2 : launch_cfg.controller_p34;
                if (sm == Switch2PlayerSlotMode::Auto &&
                    pads[static_cast<std::size_t>(p)].physical_style !=
                        player_bindings[static_cast<std::size_t>(p)].last_physical_style) {
                    need_ctrl_reload = true;
                }
            }
            if (need_ctrl_reload) {
                const bool old_docked =
                    (Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked);
                ConfigureSwitch2Controllers(launch_cfg, pads, player_bindings, true);
                system->HIDCore().ReloadInputDevices();
                refresh_emulated_ctrls();
                const bool new_docked =
                    (Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked);
                launch_cfg.docked_mode = new_docked;
                if (old_docked != new_docked) {
                    system->GetAppletManager().OperationModeChanged();
                }
            }

            const auto& pad = pads[0];

            // Handle pending Software Keyboard (swkbd) requests from guest HLE threads.
            {
                bool has_pending = false;
                bool req_close = false;
                bool req_is_inline = false;
                Core::Frontend::KeyboardInitializeParameters req_params{};
                Core::Frontend::InlineAppearParameters req_inline_params{};
                std::u16string req_inline_text;
                Core::Frontend::SoftwareKeyboardApplet::SubmitNormalCallback req_norm_cb;
                Core::Frontend::SoftwareKeyboardApplet::SubmitInlineCallback req_inline_cb;
                {
                    std::scoped_lock lk{kbd_bridge->mutex};
                    if (kbd_bridge->close_requested) {
                        req_close = true;
                        kbd_bridge->close_requested = false;
                    }
                    if (!kbd_overlay_active && kbd_bridge->pending) {
                        has_pending = true;
                        kbd_bridge->pending = false;
                        req_is_inline = kbd_bridge->is_inline;
                        req_params = kbd_bridge->params;
                        req_inline_params = kbd_bridge->inline_params;
                        req_inline_text = kbd_bridge->inline_text;
                        req_norm_cb = std::move(kbd_bridge->submit_normal);
                        req_inline_cb = std::move(kbd_bridge->submit_inline);
                    }
                }
                if (req_close && kbd_overlay_active) {
                    kbd_norm_cb = nullptr;
                    kbd_inline_cb = nullptr;
                    kbd_overlay_active = false;
                    Switch2HideKeyboardOverlay();
                }
                if (has_pending) {
                    if (quick_menu_open) {
                        quick_menu_open = false;
                        Switch2HideQuickMenuOverlay();
                    }
                    neutralize_all_inputs();

                    kbd_is_inline = req_is_inline;
                    kbd_norm_cb = std::move(req_norm_cb);
                    kbd_inline_cb = std::move(req_inline_cb);

                    const std::string header_s = Common::UTF16ToUTF8(req_params.header_text);
                    const std::string sub_s = Common::UTF16ToUTF8(req_params.sub_text);
                    const std::string guide_s = Common::UTF16ToUTF8(req_params.guide_text);
                    const std::string ok_s = Common::UTF16ToUTF8(req_params.ok_text);
                    const std::string init_s = Common::UTF16ToUTF8(
                        (req_is_inline && !req_inline_text.empty()) ? req_inline_text
                                                                    : req_params.initial_text);

                    const u32 raw_max = (req_is_inline && req_inline_params.max_text_length > 0)
                                            ? req_inline_params.max_text_length
                                            : req_params.max_text_length;
                    const u32 raw_min = (req_is_inline && req_inline_params.min_text_length > 0)
                                            ? req_inline_params.min_text_length
                                            : req_params.min_text_length;
                    kbd_max_len = (raw_max > 0) ? std::clamp<int>(static_cast<int>(raw_max), 1, 500)
                                                : 32;
                    kbd_min_len = std::clamp<int>(static_cast<int>(raw_min), 0, kbd_max_len);

                    const auto sw_type =
                        req_is_inline ? req_inline_params.type : req_params.type;
                    int kbd_type = 0;
                    if (sw_type == Service::AM::Frontend::SwkbdType::NumberPad) {
                        kbd_type = 1;
                    } else if (sw_type == Service::AM::Frontend::SwkbdType::Qwerty) {
                        kbd_type = 2;
                    }

                    kbd_header = !header_s.empty() ? header_s
                                                   : (!sub_s.empty() ? sub_s : guide_s);
                    kbd_text = init_s;
                    if (static_cast<int>(kbd_text.size()) > kbd_max_len) {
                        kbd_text.resize(static_cast<std::size_t>(kbd_max_len));
                    }

                    bool open_overlay = launch_cfg.kbd_force_overlay;
                    if (!open_overlay) {
                        Switch2KbdRequest kreq{};
                        kreq.header_text = header_s.c_str();
                        kreq.sub_text = sub_s.c_str();
                        kreq.guide_text = guide_s.c_str();
                        kreq.ok_text = ok_s.c_str();
                        kreq.initial_text = kbd_text.c_str();
                        kreq.max_len = kbd_max_len;
                        kreq.kbd_type = kbd_type;
                        kreq.password_mode =
                            (req_params.password_mode ==
                             Service::AM::Frontend::SwkbdPasswordMode::Enabled);
                        char out_utf8[1024]{};
                        const int st = Switch2ShowNativeKeyboard(&kreq, out_utf8, sizeof(out_utf8));
                        if (st == SWITCH2_KBD_OK) {
                            submit_kbd_result(true, std::string(out_utf8));
                        } else if (st == SWITCH2_KBD_CANCEL) {
                            submit_kbd_result(false, "");
                        } else {
                            open_overlay = true;
                        }
                    }

                    if (open_overlay) {
                        kbd_overlay_active = true;
                        kbd_shift = kbd_text.empty();
                        kbd_symbols = (kbd_type == 1);
                        kbd_row = 0;
                        kbd_col = 0;
                        kbd_prev_touch = pad.has_touch;
                        Switch2RenderKeyboardOverlay(kbd_header, kbd_text, kbd_max_len, kbd_row,
                                                     kbd_col, kbd_shift, kbd_symbols,
                                                     launch_cfg.ui_english);
                    }
                }
            }

            const bool lr_plus_combo =
                (pad.raw_held & SWITCH2_BTN_L) && (pad.raw_held & SWITCH2_BTN_R) &&
                (pad.raw_held & SWITCH2_BTN_PLUS) &&
                ((pad.raw_down & (SWITCH2_BTN_L | SWITCH2_BTN_R | SWITCH2_BTN_PLUS)) != 0);
            if (kbd_overlay_active) {
                const std::uint64_t down = pad.raw_down;
                bool redraw_kbd = false;
                if (down & SWITCH2_BTN_UP) {
                    kbd_row = (kbd_row + 3) % 4;
                    redraw_kbd = true;
                } else if (down & SWITCH2_BTN_DOWN) {
                    kbd_row = (kbd_row + 1) % 4;
                    redraw_kbd = true;
                } else if (down & SWITCH2_BTN_LEFT) {
                    kbd_col = (kbd_col + 9) % 10;
                    redraw_kbd = true;
                } else if (down & SWITCH2_BTN_RIGHT) {
                    kbd_col = (kbd_col + 1) % 10;
                    redraw_kbd = true;
                } else if (down & SWITCH2_BTN_A) {
                    if (static_cast<int>(kbd_text.size()) < kbd_max_len) {
                        kbd_text.push_back(
                            Switch2KeyboardCharAt(kbd_row, kbd_col, kbd_shift, kbd_symbols));
                        if (kbd_shift && !kbd_symbols) kbd_shift = false;
                        redraw_kbd = true;
                    }
                } else if (down & SWITCH2_BTN_B) {
                    if (!kbd_text.empty()) {
                        kbd_text.pop_back();
                        redraw_kbd = true;
                    }
                } else if (down & SWITCH2_BTN_Y) {
                    if (static_cast<int>(kbd_text.size()) < kbd_max_len) {
                        kbd_text.push_back(' ');
                        redraw_kbd = true;
                    }
                } else if (down & SWITCH2_BTN_L) {
                    if (kbd_symbols) {
                        kbd_symbols = false;
                    } else {
                        kbd_shift = !kbd_shift;
                    }
                    redraw_kbd = true;
                } else if (down & SWITCH2_BTN_R) {
                    kbd_symbols = !kbd_symbols;
                    redraw_kbd = true;
                } else if (down & SWITCH2_BTN_PLUS) {
                    if (static_cast<int>(kbd_text.size()) >= kbd_min_len) {
                        submit_kbd_result(true, kbd_text);
                    }
                } else if (down & SWITCH2_BTN_MINUS) {
                    submit_kbd_result(false, "");
                }

                if (kbd_overlay_active && pad.has_touch && !kbd_prev_touch) {
                    std::uint32_t win_w = 1280;
                    std::uint32_t win_h = 720;
                    Switch2GetWindowDimensions(&win_w, &win_h);
                    const int tx = static_cast<int>(pad.touch_x * static_cast<float>(win_w));
                    const int ty = static_cast<int>(pad.touch_y * static_cast<float>(win_h));
                    const int ox = (static_cast<int>(win_w) - 820) / 2;
                    const int oy = (static_cast<int>(win_h) - 476) / 2;
                    const int lx = tx - ox;
                    const int ly = ty - oy;

                    if (lx >= 24 && lx < 24 + 10 * 77 && ly >= 122 && ly < 122 + 4 * 64) {
                        const int tc = (lx - 24) / 77;
                        const int tr = (ly - 122) / 64;
                        const int rx = (lx - 24) % 77;
                        const int ry = (ly - 122) % 64;
                        if (tc >= 0 && tc < 10 && tr >= 0 && tr < 4 && rx < 71 && ry < 56) {
                            kbd_row = tr;
                            kbd_col = tc;
                            if (static_cast<int>(kbd_text.size()) < kbd_max_len) {
                                kbd_text.push_back(Switch2KeyboardCharAt(kbd_row, kbd_col,
                                                                         kbd_shift, kbd_symbols));
                                if (kbd_shift && !kbd_symbols) kbd_shift = false;
                            }
                            redraw_kbd = true;
                        }
                    } else if (ly >= 386 && ly < 428) {
                        if (lx >= 24 && lx < 204) {
                            if (kbd_symbols) {
                                kbd_symbols = false;
                            } else {
                                kbd_shift = !kbd_shift;
                            }
                            redraw_kbd = true;
                        } else if (lx >= 214 && lx < 404) {
                            if (static_cast<int>(kbd_text.size()) < kbd_max_len) {
                                kbd_text.push_back(' ');
                                redraw_kbd = true;
                            }
                        } else if (lx >= 414 && lx < 604) {
                            if (!kbd_text.empty()) {
                                kbd_text.pop_back();
                                redraw_kbd = true;
                            }
                        } else if (lx >= 614 && lx < 796) {
                            if (static_cast<int>(kbd_text.size()) >= kbd_min_len) {
                                submit_kbd_result(true, kbd_text);
                            }
                        }
                    }
                }
                kbd_prev_touch = pad.has_touch;

                if (redraw_kbd && kbd_overlay_active) {
                    Switch2RenderKeyboardOverlay(kbd_header, kbd_text, kbd_max_len, kbd_row,
                                                 kbd_col, kbd_shift, kbd_symbols,
                                                 launch_cfg.ui_english);
                }
            } else if (lr_plus_combo) {
                quick_menu_open = !quick_menu_open;
                quick_msg.clear();
                if (quick_menu_open) {
                    neutralize_all_inputs();
                    Switch2RenderQuickMenuOverlay(launch_cfg, volume_pct, quick_sel, quick_msg);
                } else {
                    Switch2HideQuickMenuOverlay();
                }
            } else if (quick_menu_open) {
                const std::uint64_t down = pad.raw_down;
                bool redraw_qm = false;
                if (down & SWITCH2_BTN_B) {
                    quick_menu_open = false;
                    Switch2HideQuickMenuOverlay();
                } else if (down & (SWITCH2_BTN_UP | SWITCH2_BTN_DOWN)) {
                    constexpr int kQuickCount = 14;
                    if (down & SWITCH2_BTN_UP) quick_sel = (quick_sel + kQuickCount - 1) % kQuickCount;
                    if (down & SWITCH2_BTN_DOWN) quick_sel = (quick_sel + 1) % kQuickCount;
                    quick_msg.clear();
                    redraw_qm = true;
                } else if (down & (SWITCH2_BTN_LEFT | SWITCH2_BTN_RIGHT | SWITCH2_BTN_A)) {
                    const int dir = (down & SWITCH2_BTN_LEFT) ? -1 : 1;
                    bool setting_changed = false;
                    bool docked_changed = false;
                    bool ctrl_changed = false;
                    switch (quick_sel) {
                    case 0: {
                        static constexpr std::array<int, 4> kSpeeds{{100, 0, 150, 200}};
                        int idx = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (kSpeeds[static_cast<std::size_t>(i)] == launch_cfg.speed_limit) {
                                idx = i;
                                break;
                            }
                        }
                        idx = (idx + dir + 4) % 4;
                        launch_cfg.speed_limit = kSpeeds[static_cast<std::size_t>(idx)];
                        setting_changed = true;
                        break;
                    }
                    case 1:
                        launch_cfg.vsync_mode = (launch_cfg.vsync_mode + dir + 4) % 4;
                        setting_changed = true;
                        break;
                    case 2:
                        launch_cfg.scaling_filter = (launch_cfg.scaling_filter + dir + 7) % 7;
                        setting_changed = true;
                        break;
                    case 3:
                        launch_cfg.anti_aliasing = (launch_cfg.anti_aliasing + dir + 3) % 3;
                        setting_changed = true;
                        break;
                    case 4:
                        launch_cfg.docked_mode = !launch_cfg.docked_mode;
                        setting_changed = true;
                        docked_changed = true;
                        break;
                    case 5:
                        launch_cfg.gpu_fast = !launch_cfg.gpu_fast;
                        setting_changed = true;
                        break;
                    case 6:
                        launch_cfg.cpu_boost = !launch_cfg.cpu_boost;
                        setting_changed = true;
                        break;
                    case 7:
                        launch_cfg.show_fps_mode = (launch_cfg.show_fps_mode + dir + 3) % 3;
                        setting_changed = true;
                        break;
                    case 8:
                        volume_pct = std::clamp(volume_pct + dir * 10, 0, 150);
                        setting_changed = true;
                        break;
                    case 9: {
                        static constexpr std::array<Switch2ControllerProfile, 6> kP1Profiles{{
                            Switch2ControllerProfile::ProController,
                            Switch2ControllerProfile::Auto,
                            Switch2ControllerProfile::Handheld,
                            Switch2ControllerProfile::DualJoycon,
                            Switch2ControllerProfile::RightJoycon,
                            Switch2ControllerProfile::LeftJoycon,
                        }};
                        int idx = 0;
                        for (int i = 0; i < 6; ++i) {
                            if (kP1Profiles[static_cast<std::size_t>(i)] == launch_cfg.controller_profile) {
                                idx = i;
                                break;
                            }
                        }
                        idx = (idx + dir + 6) % 6;
                        launch_cfg.controller_profile = kP1Profiles[static_cast<std::size_t>(idx)];
                        ctrl_changed = true;
                        break;
                    }
                    case 10: {
                        static constexpr std::array<Switch2PlayerSlotMode, 6> kP2Modes{{
                            Switch2PlayerSlotMode::Auto,
                            Switch2PlayerSlotMode::Off,
                            Switch2PlayerSlotMode::ProController,
                            Switch2PlayerSlotMode::DualJoycon,
                            Switch2PlayerSlotMode::RightJoycon,
                            Switch2PlayerSlotMode::LeftJoycon,
                        }};
                        int idx = 0;
                        for (int i = 0; i < 6; ++i) {
                            if (kP2Modes[static_cast<std::size_t>(i)] == launch_cfg.controller_p2) {
                                idx = i;
                                break;
                            }
                        }
                        idx = (idx + dir + 6) % 6;
                        launch_cfg.controller_p2 = kP2Modes[static_cast<std::size_t>(idx)];
                        ctrl_changed = true;
                        break;
                    }
                    case 11:
                        if (down & SWITCH2_BTN_A) {
                            const bool ok = Switch2SavePerGameQuickConfig(launch_cfg);
                            quick_msg =
                                ok ? (launch_cfg.ui_english ? "Saved to game profile!"
                                                            : "Salvo no perfil do jogo!")
                                   : (launch_cfg.ui_english ? "Error saving profile!"
                                                            : "Erro ao salvar perfil!");
                            redraw_qm = true;
                        }
                        break;
                    case 12:
                        if (down & SWITCH2_BTN_A) {
                            exit_to_launcher = true;
                            quick_menu_open = false;
                            Switch2HideQuickMenuOverlay();
                        }
                        break;
                    case 13:
                        if (down & SWITCH2_BTN_A) {
                            quick_menu_open = false;
                            Switch2HideQuickMenuOverlay();
                        }
                        break;
                    default:
                        break;
                    }
                    if (setting_changed) {
                        ApplySwitch2LiveSettings(*system, launch_cfg, volume_pct, docked_changed);
                        quick_msg.clear();
                        redraw_qm = true;
                    }
                    if (ctrl_changed) {
                        const bool old_docked =
                            (Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked);
                        ConfigureSwitch2Controllers(launch_cfg, pads, player_bindings, true);
                        system->HIDCore().ReloadInputDevices();
                        refresh_emulated_ctrls();
                        const bool new_docked =
                            (Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked);
                        launch_cfg.docked_mode = new_docked;
                        if (old_docked != new_docked) {
                            system->GetAppletManager().OperationModeChanged();
                        }
                        quick_msg.clear();
                        redraw_qm = true;
                    }
                }
                if (exit_to_launcher) {
                    break;
                }
                if (redraw_qm && quick_menu_open) {
                    Switch2RenderQuickMenuOverlay(launch_cfg, volume_pct, quick_sel, quick_msg);
                }
            } else {
                for (int p = 0; p < 4; ++p) {
                    const auto idx = static_cast<std::size_t>(p);
                    const auto& b = player_bindings[idx];
                    if (!b.enabled) {
                        continue;
                    }
                    ForwardSwitch2Input(*input_subsystem, pads[idx], prev_pads[idx], b.pad_index,
                                        b.effective_profile, p == 0, prev_touch);
                    if (auto* ctrl = active_ctrls[idx]; ctrl != nullptr) {
                        const auto vl = ctrl->GetActualVibrationValue(Core::HID::DeviceIndex::Left);
                        const auto vr = ctrl->GetActualVibrationValue(Core::HID::DeviceIndex::Right);
                        const bool diff_l =
                            vl.low_amplitude != last_l[idx].low_amplitude ||
                            vl.high_amplitude != last_l[idx].high_amplitude ||
                            ((vl.low_amplitude != 0.0f || vl.high_amplitude != 0.0f) &&
                             (vl.low_frequency != last_l[idx].low_frequency ||
                              vl.high_frequency != last_l[idx].high_frequency));
                        const bool diff_r =
                            vr.low_amplitude != last_r[idx].low_amplitude ||
                            vr.high_amplitude != last_r[idx].high_amplitude ||
                            ((vr.low_amplitude != 0.0f || vr.high_amplitude != 0.0f) &&
                             (vr.low_frequency != last_r[idx].low_frequency ||
                              vr.high_frequency != last_r[idx].high_frequency));
                        if (diff_l || diff_r) {
                            Switch2SetPlayerVibration(p, vl.low_amplitude, vl.low_frequency,
                                                      vl.high_amplitude, vl.high_frequency,
                                                      vr.low_amplitude, vr.low_frequency,
                                                      vr.high_amplitude, vr.high_frequency);
                            last_l[idx] = vl;
                            last_r[idx] = vr;
                        }
                    }
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
        // If Horizon didn't request exit (e.g. Home/Close), the user held [+]+[-] or chose Return -> relaunch UI.
        should_return_to_launcher = exit_to_launcher || Switch2MainLoop();
    } else {
        // Save a detailed load-error diagnostic so the launcher displays the exact reason on screen.
        std::string err_desc;
        switch (load_result) {
        case Core::SystemResultStatus::ErrorGetLoader:
            err_desc = "ErrorGetLoader: Could not identify ROM format";
            break;
        case Core::SystemResultStatus::ErrorLoader:
            err_desc = "ErrorLoader: Failed to load ROM";
            break;
        case Core::SystemResultStatus::ErrorNotInitialized:
            err_desc = "ErrorNotInitialized: CPU core not initialized";
            break;
        case Core::SystemResultStatus::ErrorVideoCore:
            err_desc = "ErrorVideoCore: Failed to initialize Vulkan";
            break;
        default: {
            const u16 loader_id = static_cast<u16>(Core::SystemResultStatus::ErrorLoader);
            const u16 raw = static_cast<u16>(load_result);
            if (raw >= loader_id) {
                const u16 err_id = raw - loader_id;
                char code_buf[32];
                std::snprintf(code_buf, sizeof(code_buf), "Code %04X-%04X: ", loader_id, err_id);
                err_desc = std::string(code_buf) +
                           Loader::GetResultStatusString(static_cast<Loader::ResultStatus>(err_id));
            } else {
                err_desc = "SystemResultStatus=" + std::to_string(static_cast<int>(load_result));
            }
            break;
        }
        }
        const auto probe_file = Core::GetGameFileFromPath(system->GetFilesystem(), launch_cfg.rom_path);
        std::string probe_line;
        std::string magic_line;
        if (probe_file) {
            const double mb = static_cast<double>(probe_file->GetSize()) / (1024.0 * 1024.0);
            char pbuf[160];
            std::snprintf(pbuf, sizeof(pbuf), "VFS OK: %.1f MB | Name: %s", mb,
                          probe_file->GetName().c_str());
            probe_line = pbuf;
            std::array<u8, 0x104> hdr{};
            const std::size_t n = probe_file->Read(hdr.data(), hdr.size(), 0);
            char mbuf[128];
            std::snprintf(mbuf, sizeof(mbuf),
                          "Hdr(%zuB): %02X %02X %02X %02X | @100: %02X %02X %02X %02X", n,
                          hdr[0], hdr[1], hdr[2], hdr[3], hdr[0x100], hdr[0x101], hdr[0x102],
                          hdr[0x103]);
            magic_line = mbuf;
        } else {
            std::error_code pec;
            const bool is_d = std::filesystem::is_directory(launch_cfg.rom_path, pec);
            const bool has_00 =
                std::filesystem::is_regular_file(std::filesystem::path(launch_cfg.rom_path) / "00", pec);
            char pbuf[128];
            std::snprintf(pbuf, sizeof(pbuf), "VFS NULL! is_dir=%d has_00=%d", is_d ? 1 : 0,
                          has_00 ? 1 : 0);
            probe_line = pbuf;
            magic_line = "Check ROM path / split files on SD card.";
        }
        if (FILE* ef = std::fopen("sdmc:/switch/eden-nx2/last_load_error.txt", "w")) {
            const std::string fname = std::filesystem::path(launch_cfg.rom_path).filename().string();
            std::fprintf(ef, "%s\n%s\n%s\n%s\n", fname.c_str(), err_desc.c_str(),
                         probe_line.c_str(), magic_line.c_str());
            std::fflush(ef);
            std::fclose(ef);
        }
        // Return cleanly to the launcher when a title fails to load.
        should_return_to_launcher = Switch2MainLoop();
    }

    Switch2BootTrace("99: shutdown");
    // Restore thread priority & speed limit BEFORE pausing CoreTiming so unlocked speed limit
    // cannot starve shutdown or keep HostTiming spinning at 6,000 Hz.
    Common::SetCurrentThreadPriority(Common::ThreadPriority::High);
    Settings::values.use_speed_limit.SetValue(true);
    Settings::values.speed_limit.SetValue(100);
    Switch2HideKeyboardOverlay();
    Switch2HideQuickMenuOverlay();
    UpdateSwitch2FpsOverlay("");
    Switch2SetCpuBoost(false);
    for (int p = 0; p < 4; ++p) {
        Switch2SetPlayerVibration(p, 0, 160.0f, 0, 320.0f, 0, 160.0f, 0, 320.0f);
    }
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

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
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "dynarmic/backend/arm64/switch2_code_block.h"
extern "C" {
void* Switch2GetNativeWindow(void);
void Switch2GetWindowDimensions(std::uint32_t* w, std::uint32_t* h);
void Switch2SleepMs(std::uint64_t ms);
bool Switch2MainLoop(void);
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

    std::error_code ec;
    std::filesystem::create_directories("sdmc:/switch/eden-nx2/roms", ec);

    Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
    Settings::values.cpu_backend.SetValue(Settings::CpuBackend::Dynarmic);
    Settings::values.cpuopt_fastmem.SetValue(false);
    auto& player1 = Settings::values.players.GetValue()[0];
    player1.connected = true;
    player1.controller_type = Settings::ControllerType::ProController;

    std::string rom_path;
    if (std::filesystem::exists("sdmc:/switch/eden-nx2/roms", ec)) {
        for (const auto& entry : std::filesystem::directory_iterator("sdmc:/switch/eden-nx2/roms", ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (ext == ".nsp" || ext == ".xci" || ext == ".nro") {
                rom_path = entry.path().string();
                break;
            }
        }
    }

    if (rom_path.empty()) {
        Switch2BootTrace("02: No ROM found in sdmc:/switch/eden-nx2/roms");
        return 0;
    }

    {
        std::string msg = "02: ROM=" + rom_path;
        Switch2BootTrace(msg.c_str());
    }

    auto system = std::make_unique<Core::System>();
    auto input_subsystem = std::make_unique<InputCommon::InputSubsystem>();
    input_subsystem->Initialize();
    auto emu_window = std::make_unique<EmuWindowSwitch2>();
    Switch2BootTrace("03: System/Window created");

    system->Initialize();
    system->ApplySettings();
    system->SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system->SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    system->GetFileSystemController().CreateFactories(*system->GetFilesystem());
    system->GetUserChannel().clear();
    Switch2BootTrace("04: System initialized");

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    const Core::SystemResultStatus load_result =
        system->Load(*emu_window, rom_path, load_parameters);
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "05: system.Load result=%d", static_cast<int>(load_result));
        Switch2BootTrace(buf);
    }

    if (load_result == Core::SystemResultStatus::Success) {
        system->GPU().Start();
        system->GetCpuManager().OnGpuReady();

        if (Settings::values.use_disk_shader_cache.GetValue()) {
            system->Renderer().ReadRasterizer()->LoadDiskResources(
                system->GetApplicationProcessProgramID(), std::stop_token{},
                [](VideoCore::LoadCallbackStage, size_t, size_t) {});
        }

        system->RegisterExitCallback([] {});
        void(system->Run());
        Switch2BootTrace("06: system.Run() ok");

        while (Switch2MainLoop()) {
            Switch2SleepMs(16);
        }
    }

    Switch2BootTrace("99: shutdown");
    system->DetachDebugger();
    if (system->IsPoweredOn()) {
        system->Pause();
        system->ShutdownMainProcess();
    }
    emu_window.reset();
    input_subsystem->Shutdown();
    input_subsystem.reset();
    system.reset();
    Dynarmic::Backend::Arm64::Switch2JitPool::Shutdown();
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

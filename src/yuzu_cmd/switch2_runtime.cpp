// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Isolated libnx runtime hooks for Switch 2 so <switch.h> never collides with Eden's
// global `Service` namespace or `u128` type.

#ifdef __SWITCH2__

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <malloc.h>
#include <string>

#define u128 libnx_u128
#define s128 libnx_s128
#include <switch.h>
#include <switch2/memory/heap.h>
#undef u128
#undef s128

#include "yuzu_cmd/switch2_runtime.h"

namespace {

constexpr int kMaxSwitch2Players = 4;
PadState g_pads[kMaxSwitch2Players]{};
bool g_pad_initialized = false;

struct Switch2SensorAndMotorState {
    HidNpadIdType id{};
    u32 style{};
    bool valid{};
    HidSixAxisSensorHandle sensors[2]{};
    int sensor_count{};
    HidVibrationDeviceHandle motors[2]{};
    int motor_count{};
    u64 last_motion_tick{};
};

Switch2SensorAndMotorState g_pad_devs[kMaxSwitch2Players]{};
u64 g_plus_minus_hold_start_tick = 0;

Framebuffer g_fb{};
bool g_fb_initialized = false;

std::uint8_t ClassifyPhysicalStyle(u32 style_set) {
    if (style_set & HidNpadStyleTag_NpadHandheld) return SWITCH2_STYLE_HANDHELD;
    if (style_set & HidNpadStyleTag_NpadFullKey) return SWITCH2_STYLE_PRO;
    if (style_set & HidNpadStyleTag_NpadJoyDual) return SWITCH2_STYLE_JOY_DUAL;
    if (style_set & HidNpadStyleTag_NpadJoyLeft) return SWITCH2_STYLE_JOY_LEFT;
    if (style_set & HidNpadStyleTag_NpadJoyRight) return SWITCH2_STYLE_JOY_RIGHT;
    return SWITCH2_STYLE_NONE;
}

void AcquireSensorsAndMotors(int idx, HidNpadIdType id, u32 style_set) {
    auto& dev = g_pad_devs[idx];
    for (int i = 0; i < dev.sensor_count; ++i) {
        hidStopSixAxisSensor(dev.sensors[i]);
    }
    dev = {};
    dev.id = id;
    dev.style = style_set;
    dev.valid = true;

    HidNpadStyleTag style{};
    int sensor_count = 1;
    int motor_count = 2;
    if (style_set & HidNpadStyleTag_NpadHandheld) {
        style = HidNpadStyleTag_NpadHandheld;
    } else if (style_set & HidNpadStyleTag_NpadFullKey) {
        style = HidNpadStyleTag_NpadFullKey;
    } else if (style_set & HidNpadStyleTag_NpadJoyDual) {
        style = HidNpadStyleTag_NpadJoyDual;
        sensor_count = 2;
    } else if (style_set & HidNpadStyleTag_NpadJoyLeft) {
        style = HidNpadStyleTag_NpadJoyLeft;
        sensor_count = 1;
        motor_count = 1;
    } else if (style_set & HidNpadStyleTag_NpadJoyRight) {
        style = HidNpadStyleTag_NpadJoyRight;
        sensor_count = 1;
        motor_count = 1;
    } else {
        return;
    }

    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(dev.sensors, sensor_count, id, style))) {
        dev.sensor_count = sensor_count;
        for (int i = 0; i < sensor_count; ++i) {
            hidStartSixAxisSensor(dev.sensors[i]);
        }
    }
    if (R_SUCCEEDED(hidInitializeVibrationDevices(dev.motors, motor_count, id, style))) {
        dev.motor_count = motor_count;
    }
}

void FillPlayerSnapshot(int idx, Switch2PadSnapshot* out, bool include_touch) {
    if (idx < 0 || idx >= kMaxSwitch2Players || out == nullptr) {
        return;
    }
    auto& pad = g_pads[idx];
    auto& dev = g_pad_devs[idx];
    padUpdate(&pad);

    static constexpr HidNpadIdType kSlotIds[kMaxSwitch2Players] = {
        HidNpadIdType_No1,
        HidNpadIdType_No2,
        HidNpadIdType_No3,
        HidNpadIdType_No4,
    };
    const HidNpadIdType id =
        (idx == 0 && padIsHandheld(&pad)) ? HidNpadIdType_Handheld : kSlotIds[idx];
    const u32 style_set = padGetStyleSet(&pad);
    const std::uint8_t phys = ClassifyPhysicalStyle(style_set);
    out->connected = (phys != SWITCH2_STYLE_NONE);
    out->physical_style = phys;

    if (!dev.valid || dev.id != id || dev.style != style_set) {
        AcquireSensorsAndMotors(idx, id, style_set);
    }

    if (!out->connected) {
        return;
    }

    const u64 held = padGetButtons(&pad);
    const u64 down = padGetButtonsDown(&pad);

    std::uint32_t mapped_buttons = static_cast<std::uint32_t>(held & 0xFFFFu);
    if (held & HidNpadButton_AnySL) {
        mapped_buttons |= (1u << 16);
    }
    if (held & HidNpadButton_AnySR) {
        mapped_buttons |= (1u << 17);
    }
    out->buttons = mapped_buttons;

    std::uint64_t mapped_down = down & 0xFFFFULL;
    if (down & HidNpadButton_AnySL) {
        mapped_down |= SWITCH2_BTN_SL;
    }
    if (down & HidNpadButton_AnySR) {
        mapped_down |= SWITCH2_BTN_SR;
    }
    out->raw_down = mapped_down;

    std::uint64_t mapped_held = held & 0xFFFFULL;
    if (held & HidNpadButton_AnySL) {
        mapped_held |= SWITCH2_BTN_SL;
    }
    if (held & HidNpadButton_AnySR) {
        mapped_held |= SWITCH2_BTN_SR;
    }
    out->raw_held = mapped_held;

    const HidAnalogStickState left = padGetStickPos(&pad, 0);
    const HidAnalogStickState right = padGetStickPos(&pad, 1);
    constexpr float kStickMax = static_cast<float>(JOYSTICK_MAX);
    out->left_x = static_cast<float>(left.x) / kStickMax;
    out->left_y = static_cast<float>(left.y) / kStickMax;
    out->right_x = static_cast<float>(right.x) / kStickMax;
    out->right_y = static_cast<float>(right.y) / kStickMax;

    // Virtual stick navigation bits (matching D-Pad style for launcher/menu navigation)
    if (down & HidNpadButton_StickLLeft)  out->raw_down |= SWITCH2_BTN_LEFT;
    if (down & HidNpadButton_StickLUp)    out->raw_down |= SWITCH2_BTN_UP;
    if (down & HidNpadButton_StickLRight) out->raw_down |= SWITCH2_BTN_RIGHT;
    if (down & HidNpadButton_StickLDown)  out->raw_down |= SWITCH2_BTN_DOWN;

    HidSixAxisSensorState motion{};
    if (dev.sensor_count > 0 &&
        hidGetSixAxisSensorStates(dev.sensors[dev.sensor_count - 1], &motion, 1) > 0) {
        const u64 tick = armGetSystemTick();
        out->has_motion = true;
        out->motion_delta_us =
            dev.last_motion_tick != 0 ? armTicksToNs(tick - dev.last_motion_tick) / 1000 : 0;
        dev.last_motion_tick = tick;
        out->gyro[0] = motion.angular_velocity.x;
        out->gyro[1] = motion.angular_velocity.y;
        out->gyro[2] = motion.angular_velocity.z;
        out->accel[0] = motion.acceleration.x;
        out->accel[1] = motion.acceleration.y;
        out->accel[2] = motion.acceleration.z;
    }

    if (include_touch) {
        HidTouchScreenState touch{};
        if (hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0) {
            out->has_touch = true;
            out->touch_x = static_cast<float>(touch.touches[0].x) / 1280.0f;
            out->touch_y = static_cast<float>(touch.touches[0].y) / 720.0f;
        }
    }
}

} // namespace

extern "C" {

extern char __start__[];

struct __pthread_t;
int __syscall_thread_detach(struct __pthread_t*);
void __libnx_exception_handler(ThreadExceptionDump* ctx);

int __syscall_thread_detach(struct __pthread_t*) {
    // newlib's pthread_detach calls this syscall hook; libnx omits it (ENOSYS),
    // which causes every std::thread::detach() in Eden to throw std::system_error.
    return 0;
}

alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    const u64 base = reinterpret_cast<u64>(__start__);
    std::FILE* const file = std::fopen("sdmc:/switch/eden-nx2/crash.txt", "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "error_desc: 0x%x\n", ctx->error_desc);
    std::fprintf(file, "esr: 0x%x\n", ctx->esr);
    std::fprintf(file, "far: 0x%lx\n", ctx->far.x);
    std::fprintf(file, "module base: 0x%lx\n", base);
    std::fprintf(file, "pc: 0x%lx (offset 0x%lx)\n", ctx->pc.x, ctx->pc.x - base);
    std::fprintf(file, "lr: 0x%lx (offset 0x%lx)\n", ctx->lr.x, ctx->lr.x - base);
    std::fprintf(file, "sp: 0x%lx\n", ctx->sp.x);
    std::fprintf(file, "fp: 0x%lx\n", ctx->fp.x);
    for (int i = 0; i < 29; ++i) {
        std::fprintf(file, "x%d: 0x%lx\n", i, ctx->cpu_gprs[i].x);
    }
    std::fflush(file);

    u64 fp = ctx->fp.x;
    for (int depth = 0; depth < 32 && fp != 0 && (fp & 0xF) == 0; ++depth) {
        MemoryInfo info{};
        u32 page_info = 0;
        if (R_FAILED(svcQueryMemory(&info, &page_info, fp)) || !(info.perm & Perm_R)) {
            break;
        }
        const u64* const frame = reinterpret_cast<const u64*>(fp);
        std::fprintf(file, "frame %d: 0x%lx (offset 0x%lx)\n", depth, frame[1], frame[1] - base);
        if (frame[0] <= fp) {
            break;
        }
        fp = frame[0];
    }
    std::fflush(file);
    std::fclose(file);
}

void* Switch2GetNativeWindow(void) {
    return nwindowGetDefault();
}

void Switch2GetWindowDimensions(std::uint32_t* w, std::uint32_t* h) {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    nwindowGetDimensions(nwindowGetDefault(), &width, &height);
    if (width == 0 || height == 0) {
        width = 1280;
        height = 720;
    }
    *w = width;
    *h = height;
}

void Switch2SleepMs(std::uint64_t ms) {
    svcSleepThread(ms * 1000000ULL);
}

bool Switch2MainLoop(void) {
    return appletMainLoop();
}

void Switch2InitInput(void) {
    if (!g_pad_initialized) {
        padConfigureInput(kMaxSwitch2Players, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&g_pads[0]);
        padInitialize(&g_pads[1], HidNpadIdType_No2);
        padInitialize(&g_pads[2], HidNpadIdType_No3);
        padInitialize(&g_pads[3], HidNpadIdType_No4);
        hidInitializeTouchScreen();
        for (int i = 0; i < kMaxSwitch2Players; ++i) {
            g_pad_devs[i] = {};
        }
        g_pad_initialized = true;
    }
    g_plus_minus_hold_start_tick = 0;
}

bool Switch2PollInput(Switch2PadSnapshot* out, bool allow_plus_minus_exit) {
    Switch2PadSnapshot local{};
    Switch2PadSnapshot* target = (out != nullptr) ? out : &local;
    std::memset(target, 0, sizeof(*target));

    if (!appletMainLoop()) {
        return false;
    }
    if (!g_pad_initialized) {
        Switch2InitInput();
    }

    FillPlayerSnapshot(0, target, true);

    if (allow_plus_minus_exit && (target->raw_held & SWITCH2_BTN_PLUS) &&
        (target->raw_held & SWITCH2_BTN_MINUS)) {
        const u64 now = armGetSystemTick();
        if (g_plus_minus_hold_start_tick == 0) {
            g_plus_minus_hold_start_tick = now;
        } else if (armTicksToNs(now - g_plus_minus_hold_start_tick) >= 2000000000ULL) {
            g_plus_minus_hold_start_tick = 0;
            return false;
        }
    } else {
        g_plus_minus_hold_start_tick = 0;
    }

    return true;
}

void Switch2PollPlayerInput(int player_idx, Switch2PadSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    std::memset(out, 0, sizeof(*out));
    if (player_idx < 0 || player_idx >= kMaxSwitch2Players) {
        return;
    }
    if (!g_pad_initialized) {
        Switch2InitInput();
    }
    FillPlayerSnapshot(player_idx, out, player_idx == 0);
}

void Switch2SetPlayerVibration(int player_idx, float l_low_amp, float l_low_freq, float l_high_amp,
                               float l_high_freq, float r_low_amp, float r_low_freq,
                               float r_high_amp, float r_high_freq) {
    if (player_idx < 0 || player_idx >= kMaxSwitch2Players) {
        return;
    }
    auto& dev = g_pad_devs[player_idx];
    if (dev.motor_count <= 0) {
        return;
    }
    if (dev.motor_count == 1) {
        const bool use_right = (r_low_amp + r_high_amp) > (l_low_amp + l_high_amp);
        const HidVibrationValue val =
            use_right ? HidVibrationValue{r_low_amp, r_low_freq, r_high_amp, r_high_freq}
                      : HidVibrationValue{l_low_amp, l_low_freq, l_high_amp, l_high_freq};
        hidSendVibrationValues(dev.motors, &val, 1);
        return;
    }
    const HidVibrationValue values[2]{
        {l_low_amp, l_low_freq, l_high_amp, l_high_freq},
        {r_low_amp, r_low_freq, r_high_amp, r_high_freq},
    };
    hidSendVibrationValues(dev.motors, values, 2);
}

void Switch2SetVibration(float l_low_amp, float l_low_freq, float l_high_amp, float l_high_freq,
                         float r_low_amp, float r_low_freq, float r_high_amp, float r_high_freq) {
    Switch2SetPlayerVibration(0, l_low_amp, l_low_freq, l_high_amp, l_high_freq, r_low_amp,
                              r_low_freq, r_high_amp, r_high_freq);
}

void Switch2SetCpuBoost(bool enable) {
    appletSetCpuBoostMode(enable ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
}

void Switch2GetMemoryStats(std::uint64_t* out_used, std::uint64_t* out_total) {
    const u64 total = heapGetTotalSize();
    const u64 free_bytes = heapGetFreeSize();
    if (total > 0) {
        if (out_total) *out_total = total;
        if (out_used) *out_used = total >= free_bytes ? (total - free_bytes) : 0;
        return;
    }
    u64 svc_total = 0;
    u64 svc_used = 0;
    svcGetInfo(&svc_total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&svc_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (out_total) *out_total = svc_total;
    if (out_used) *out_used = svc_used;
}

std::uint64_t Switch2GetCoreMask(void) {
    u64 mask = 0;
    svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    return mask;
}

std::uint64_t Switch2GetSystemTickFreq(void) {
    // Derive exact libounce CNTPCT_EL0 frequency via armTicksToNs (never touches trapped CNTFRQ_EL0).
    const u64 ns_for_10m_ticks = armTicksToNs(10'000'000ULL);
    if (ns_for_10m_ticks > 0) {
        return (10'000'000ULL * 1'000'000'000ULL + ns_for_10m_ticks / 2) / ns_for_10m_ticks;
    }
    return 19'200'000ULL;
}

int Switch2ShowNativeKeyboard(const Switch2KbdRequest* req, char* out_utf8, int out_utf8_size) {
    if (out_utf8 == nullptr || out_utf8_size <= 1) {
        return SWITCH2_KBD_UNAVAILABLE;
    }
    out_utf8[0] = '\0';

    SwkbdConfig kbd{};
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) {
        return SWITCH2_KBD_UNAVAILABLE;
    }

    if (req != nullptr && req->password_mode) {
        swkbdConfigMakePresetPassword(&kbd);
    } else {
        swkbdConfigMakePresetDefault(&kbd);
    }

    if (req != nullptr) {
        if (req->kbd_type == 1) {
            swkbdConfigSetType(&kbd, SwkbdType_NumPad);
        } else if (req->kbd_type == 2) {
            swkbdConfigSetType(&kbd, SwkbdType_QWERTY);
        }
        int max_len = req->max_len;
        if (max_len <= 0 || max_len > out_utf8_size - 1) {
            max_len = out_utf8_size - 1;
        }
        if (max_len > 500) {
            max_len = 500;
        }
        swkbdConfigSetStringLenMax(&kbd, max_len);
        if (req->header_text && req->header_text[0] != '\0') {
            swkbdConfigSetHeaderText(&kbd, req->header_text);
        }
        if (req->sub_text && req->sub_text[0] != '\0') {
            swkbdConfigSetSubText(&kbd, req->sub_text);
        }
        if (req->guide_text && req->guide_text[0] != '\0') {
            swkbdConfigSetGuideText(&kbd, req->guide_text);
        }
        if (req->ok_text && req->ok_text[0] != '\0') {
            swkbdConfigSetOkButtonText(&kbd, req->ok_text);
        }
        if (req->initial_text && req->initial_text[0] != '\0') {
            swkbdConfigSetInitialText(&kbd, req->initial_text);
        }
        if (req->password_mode) {
            swkbdConfigSetPasswordFlag(&kbd, true);
        }
    }

    const u64 t0 = armGetSystemTick();
    rc = swkbdShow(&kbd, out_utf8, static_cast<size_t>(out_utf8_size));
    const u64 elapsed_ns = armTicksToNs(armGetSystemTick() - t0);
    swkbdClose(&kbd);

    if (R_SUCCEEDED(rc)) {
        out_utf8[out_utf8_size - 1] = '\0';
        return SWITCH2_KBD_OK;
    }
    // If Horizon OS immediately rejected launching the swkbd applet (< 250ms),
    // fall back transparently to our built-in QWERTY overlay keyboard.
    if (elapsed_ns < 250'000'000ULL) {
        return SWITCH2_KBD_UNAVAILABLE;
    }
    return SWITCH2_KBD_CANCEL;
}

bool Switch2FbInit(void) {
    if (g_fb_initialized) {
        return true;
    }
    NWindow* win = nwindowGetDefault();
    if (win == nullptr) {
        return false;
    }
    if (R_FAILED(framebufferCreate(&g_fb, win, 1280, 720, PIXEL_FORMAT_RGBA_8888, 2))) {
        return false;
    }
    if (R_FAILED(framebufferMakeLinear(&g_fb))) {
        framebufferClose(&g_fb);
        return false;
    }
    g_fb_initialized = true;
    return true;
}

std::uint32_t* Switch2FbBegin(std::uint32_t* out_stride_pixels) {
    if (!g_fb_initialized) {
        return nullptr;
    }
    u32 stride_bytes = 0;
    void* ptr = framebufferBegin(&g_fb, &stride_bytes);
    if (out_stride_pixels != nullptr) {
        *out_stride_pixels = stride_bytes / sizeof(std::uint32_t);
    }
    return reinterpret_cast<std::uint32_t*>(ptr);
}

void Switch2FbEnd(void) {
    if (g_fb_initialized) {
        framebufferEnd(&g_fb);
    }
}

void Switch2FbClose(void) {
    if (g_fb_initialized) {
        framebufferClose(&g_fb);
        g_fb = {};
        g_fb_initialized = false;
    }
}

void Switch2ScheduleReturnToLauncher(const char* argv0) {
    std::error_code ec;
    const char* target = nullptr;
    std::string argv0_str;
    if (argv0 != nullptr && argv0[0] != '\0') {
        argv0_str = argv0;
        if (argv0_str.size() > 4 && std::filesystem::exists(argv0_str, ec)) {
            target = argv0_str.c_str();
        }
    }
    if (target == nullptr) {
        static constexpr const char* kCandidates[] = {
            "sdmc:/switch/eden-nx2/eden-switch2.nro",
            "sdmc:/switch/eden-nx2/Eden-NX2.nro",
            "sdmc:/switch/eden-switch2.nro",
        };
        for (const char* cand : kCandidates) {
            if (std::filesystem::exists(cand, ec)) {
                target = cand;
                break;
            }
        }
    }
    if (target != nullptr) {
        envSetNextLoad(target, target);
    }
}

} // extern "C"

// ============================================================================
// Switch 2 Pure-C++ Demand-Committed Sparse Virtual Memory Engine
// (Zero CPU exceptions / DataAborts — safe inside NROs under nx-hbloader/kaiHEN)
// ============================================================================

namespace {

enum class SparseMode : std::uint8_t {
    None = 0,
    AliasPhys = 1, // AliasRegion (64 GiB) + svcMapPhysicalMemory
    StackMap = 2,  // StackRegion + svcMapMemory from pre-allocated heap pool
};

struct SparseRegion {
    std::atomic<bool> active{false};
    SparseMode mode{SparseMode::None};
    std::uintptr_t vbase{0};
    std::size_t vsize{0};
    std::size_t chunk_size{0};
    std::size_t num_chunks{0};
    std::atomic<std::uint8_t>* mapped{nullptr};
    std::uint32_t* mapped_slot{nullptr}; // 1-based pool slot for StackMap
    u8* pool_base{nullptr};
    std::size_t pool_chunks{0};
    std::size_t pool_used{0};
    std::size_t mapped_chunks{0};
    VirtmemReservation* reservation{nullptr};
    char tag[32]{};
};

constexpr std::size_t kMaxSparseRegions = 16;
SparseRegion g_sparse_regions[kMaxSparseRegions]{};
Mutex g_sparse_mgmt_mutex{};
bool g_sparse_inited = false;
bool g_sparse_atexit_registered = false;
bool g_sparse_alias_phys_ok = false;
bool g_sparse_stack_map_ok = false;
u64 g_sparse_total_mem = 0;
u64 g_sparse_used_mem = 0;
u64 g_sparse_stack_base = 0;
u64 g_sparse_stack_size = 0;
u64 g_sparse_alias_base = 0;
u64 g_sparse_alias_size = 0;
u32 g_sparse_phys_rc = 0;
u32 g_sparse_shrink_rc = 0;
std::size_t g_orig_kernel_heap_size = 0;
char g_sparse_report[320] = "uninitialized";

void Switch2RestoreLauncherHeap() {
    mutexLock(&g_sparse_mgmt_mutex);
    for (std::size_t i = 0; i < kMaxSparseRegions; ++i) {
        auto& r = g_sparse_regions[i];
        if (!r.active.load(std::memory_order_acquire)) {
            continue;
        }
        r.active.store(false, std::memory_order_release);
        for (std::size_t c = 0; c < r.num_chunks; ++c) {
            if (r.mapped[c].load(std::memory_order_acquire) == 0) {
                continue;
            }
            void* const dst = reinterpret_cast<void*>(r.vbase + c * r.chunk_size);
            if (r.mode == SparseMode::AliasPhys) {
                svcUnmapPhysicalMemory(dst, r.chunk_size);
            } else if (r.mode == SparseMode::StackMap && r.mapped_slot != nullptr &&
                       r.mapped_slot[c] != 0) {
                void* const src =
                    r.pool_base + static_cast<std::size_t>(r.mapped_slot[c] - 1) * r.chunk_size;
                svcUnmapMemory(dst, src, r.chunk_size);
            }
        }
        r.vbase = 0;
        r.vsize = 0;
        r.mode = SparseMode::None;
    }
    if (g_orig_kernel_heap_size != 0) {
        void* tmp = nullptr;
        svcSetHeapSize(&tmp, g_orig_kernel_heap_size);
        g_orig_kernel_heap_size = 0;
    }
    mutexUnlock(&g_sparse_mgmt_mutex);
}

struct SparseExitRegistrar {
    ~SparseExitRegistrar() {
        Switch2RestoreLauncherHeap();
    }
} g_sparse_exit_registrar;

bool OverlapsSparseRegion(std::uintptr_t start, std::size_t size) {
    const std::uintptr_t end = start + size;
    for (std::size_t i = 0; i < kMaxSparseRegions; ++i) {
        if (!g_sparse_regions[i].active.load(std::memory_order_acquire)) {
            continue;
        }
        const std::uintptr_t rs = g_sparse_regions[i].vbase;
        const std::uintptr_t re = rs + g_sparse_regions[i].vsize;
        if (start < re && rs < end) {
            return true;
        }
    }
    return false;
}

std::uintptr_t FindFreeAliasSlice(std::size_t size, std::size_t align) {
    if (g_sparse_alias_base == 0 || g_sparse_alias_size < size + 2 * align) {
        return 0;
    }
    const std::uintptr_t region_end = g_sparse_alias_base + g_sparse_alias_size;
    std::uintptr_t cur = (g_sparse_alias_base + align - 1) & ~(align - 1);
    while (cur + size + align <= region_end) {
        MemoryInfo info{};
        u32 page_info = 0;
        if (R_FAILED(svcQueryMemory(&info, &page_info, cur))) {
            return 0;
        }
        const std::uintptr_t info_end = info.addr + info.size;
        if (info.type != MemType_Unmapped || info_end < cur + size + align) {
            cur = (info_end + align - 1) & ~(align - 1);
            continue;
        }
        if (OverlapsSparseRegion(cur, size + align)) {
            cur += align;
            continue;
        }
        return cur;
    }
    return 0;
}

void Switch2SparseInitUnlocked() {
    if (g_sparse_inited) {
        return;
    }
    g_sparse_inited = true;

    if (!g_sparse_atexit_registered && g_orig_kernel_heap_size != 0) {
        g_sparse_atexit_registered = true;
        std::atexit(Switch2RestoreLauncherHeap);
    }

    const bool has_override = envHasHeapOverride();
    svcGetInfo(&g_sparse_total_mem, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&g_sparse_used_mem, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&g_sparse_stack_base, InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&g_sparse_stack_size, InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&g_sparse_alias_base, InfoType_AliasRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&g_sparse_alias_size, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
    u64 alias_extra = 0;
    if (R_SUCCEEDED(svcGetInfo(&alias_extra, InfoType_AliasRegionExtraSize, CUR_PROCESS_HANDLE, 0)) &&
        alias_extra < g_sparse_alias_size) {
        g_sparse_alias_size -= alias_extra;
    }

    // Pure-syscall probe (ZERO DataAborts!) of AliasRegion + svcMapPhysicalMemory.
    const std::uintptr_t alias_probe = FindFreeAliasSlice(0x10000, 0x200000);
    if (alias_probe != 0) {
        const Result rc = svcMapPhysicalMemory(reinterpret_cast<void*>(alias_probe), 0x10000);
        g_sparse_phys_rc = rc;
        if (R_SUCCEEDED(rc)) {
            volatile std::uint64_t* p = reinterpret_cast<volatile std::uint64_t*>(alias_probe + 0x100);
            *p = 0x123456789ABCDEF0ULL;
            const bool ok = (*p == 0x123456789ABCDEF0ULL);
            svcUnmapPhysicalMemory(reinterpret_cast<void*>(alias_probe), 0x10000);
            g_sparse_alias_phys_ok = ok;
        }
    }

    // Pure-syscall probe (ZERO DataAborts!) of StackRegion + svcMapMemory.
    if (g_sparse_stack_size >= 0x400000) {
        virtmemLock();
        void* sv = virtmemFindStack(0x10000, 0x10000);
        VirtmemReservation* rv = sv ? virtmemAddReservation(sv, 0x10000) : nullptr;
        virtmemUnlock();
        u8* pool = static_cast<u8*>(memalign(0x1000, 0x10000));
        if (sv != nullptr && rv != nullptr && pool != nullptr) {
            std::memset(pool, 0, 0x10000);
            if (R_SUCCEEDED(svcMapMemory(sv, pool, 0x10000))) {
                volatile std::uint64_t* p =
                    reinterpret_cast<volatile std::uint64_t*>(static_cast<u8*>(sv) + 0x100);
                *p = 0x123456789ABCDEF0ULL;
                if (*p == 0x123456789ABCDEF0ULL) {
                    g_sparse_stack_map_ok = true;
                }
                svcUnmapMemory(sv, pool, 0x10000);
            }
        }
        if (rv != nullptr) {
            virtmemLock();
            virtmemRemoveReservation(rv);
            virtmemUnlock();
        }
        std::free(pool);
    }

    std::snprintf(g_sparse_report, sizeof(g_sparse_report),
                  "env_override=%d shrink_rc=0x%x total=%lluMiB used=%lluMiB heap=%lluMiB "
                  "free=%lluMiB stack=%lluMiB alias=%lluMiB phys_rc=0x%x phys_ok=%d stack_ok=%d",
                  has_override ? 1 : 0,
                  static_cast<unsigned int>(g_sparse_shrink_rc),
                  static_cast<unsigned long long>(g_sparse_total_mem >> 20),
                  static_cast<unsigned long long>(g_sparse_used_mem >> 20),
                  static_cast<unsigned long long>(heapGetTotalSize() >> 20),
                  static_cast<unsigned long long>(heapGetFreeSize() >> 20),
                  static_cast<unsigned long long>(g_sparse_stack_size >> 20),
                  static_cast<unsigned long long>(g_sparse_alias_size >> 20),
                  static_cast<unsigned int>(g_sparse_phys_rc),
                  g_sparse_alias_phys_ok ? 1 : 0,
                  g_sparse_stack_map_ok ? 1 : 0);

    if (std::FILE* f = std::fopen("sdmc:/switch/eden-nx2/switch2_sparse_info.txt", "w")) {
        std::fprintf(f, "%s\n", g_sparse_report);
        std::fflush(f);
        std::fclose(f);
    }
}

} // namespace

extern "C" {

void __libnx_initheap(void) {
    void* addr = nullptr;
    size_t size = 0;

    if (envHasHeapOverride()) {
        addr = envGetHeapOverrideAddr();
        size = envGetHeapOverrideSize();
        // Under kaiHEN / nx-hbloader, OverrideHeap spans [nro_end, heap_end) (~8.9 GiB).
        // Before mimalloc initializes, shrink the top of the kernel heap down to 3.5 GiB for
        // mimalloc so ~5.2 GiB returns to Horizon's pool for svcMapPhysicalMemory (12 GiB Alias DRAM).
        if (size >= (6ULL << 30)) {
            u64 heap_region_base = 0;
            if (R_SUCCEEDED(svcGetInfo(&heap_region_base, InfoType_HeapRegionAddress,
                                       CUR_PROCESS_HANDLE, 0)) &&
                heap_region_base != 0 &&
                reinterpret_cast<std::uintptr_t>(addr) >= heap_region_base) {
                const std::uintptr_t override_start = reinterpret_cast<std::uintptr_t>(addr);
                const std::uintptr_t override_end = override_start + size;
                const std::size_t orig_total = override_end - heap_region_base;
                const std::size_t nro_prefix = override_start - heap_region_base;
                const std::size_t target_mimalloc = 2176ULL << 20;
                const std::size_t target_total = (nro_prefix + target_mimalloc + 0x1FFFFFULL) & ~0x1FFFFFULL;
                if (target_total < orig_total) {
                    void* new_base = nullptr;
                    const Result rc = svcSetHeapSize(&new_base, target_total);
                    g_sparse_shrink_rc = rc;
                    if (R_SUCCEEDED(rc)) {
                        g_orig_kernel_heap_size = orig_total;
                        size = (heap_region_base + target_total) - override_start;
                    }
                }
            }
        }
    } else {
        u64 mem_available = 0;
        u64 mem_used = 0;
        svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        size_t avail = 0;
        if (mem_available > mem_used + 0x200000ULL) {
            avail = static_cast<size_t>((mem_available - mem_used - 0x200000ULL) & ~0x1FFFFFULL);
        }
        if (avail == 0) {
            avail = 0x2000000ULL * 16;
        }
        if (avail >= (6ULL << 30)) {
            size = 3584ULL << 20;
        } else {
            size = avail;
        }
        Result rc = svcSetHeapSize(&addr, size);
        if (R_FAILED(rc) && size != avail) {
            size = avail;
            rc = svcSetHeapSize(&addr, size);
        }
        if (R_FAILED(rc)) {
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
        }
    }

    extern char* fake_heap_start;
    extern char* fake_heap_end;
    fake_heap_start = static_cast<char*>(addr);
    fake_heap_end = static_cast<char*>(addr) + size;
}

void* Switch2SparseAlloc(std::size_t virtual_size, std::size_t chunk_size,
                         std::size_t physical_budget, const char* tag) {
    mutexLock(&g_sparse_mgmt_mutex);
    Switch2SparseInitUnlocked();
    if (virtual_size == 0 || chunk_size < 0x1000) {
        mutexUnlock(&g_sparse_mgmt_mutex);
        return nullptr;
    }
    virtual_size = (virtual_size + chunk_size - 1) & ~(chunk_size - 1);
    const std::size_t num_chunks = virtual_size / chunk_size;

    SparseRegion* slot = nullptr;
    for (std::size_t i = 0; i < kMaxSparseRegions; ++i) {
        if (!g_sparse_regions[i].active.load(std::memory_order_acquire) &&
            g_sparse_regions[i].vbase == 0) {
            slot = &g_sparse_regions[i];
            break;
        }
    }
    if (slot == nullptr) {
        mutexUnlock(&g_sparse_mgmt_mutex);
        return nullptr;
    }

    auto* mapped = static_cast<std::atomic<std::uint8_t>*>(
        std::calloc(num_chunks, sizeof(std::atomic<std::uint8_t>)));
    if (mapped == nullptr) {
        mutexUnlock(&g_sparse_mgmt_mutex);
        return nullptr;
    }

    // Path B (preferred): AliasRegion (64 GiB) + svcMapPhysicalMemory.
    if (g_sparse_alias_phys_ok && g_sparse_alias_size >= virtual_size + (64ULL << 20)) {
        const std::uintptr_t vbase = FindFreeAliasSlice(virtual_size, 0x200000);
        if (vbase != 0) {
            slot->mode = SparseMode::AliasPhys;
            slot->vbase = vbase;
            slot->vsize = virtual_size;
            slot->chunk_size = chunk_size;
            slot->num_chunks = num_chunks;
            slot->mapped = mapped;
            slot->mapped_slot = nullptr;
            slot->pool_base = nullptr;
            slot->pool_chunks = 0;
            slot->pool_used = 0;
            slot->mapped_chunks = 0;
            slot->reservation = nullptr;
            std::snprintf(slot->tag, sizeof(slot->tag), "%s", tag ? tag : "sparse");
            slot->active.store(true, std::memory_order_release);
            mutexUnlock(&g_sparse_mgmt_mutex);
            return reinterpret_cast<void*>(vbase);
        }
    }

    // Path A: StackRegion + svcMapMemory from pre-allocated heap pool.
    if (g_sparse_stack_map_ok && g_sparse_stack_size >= virtual_size + (64ULL << 20)) {
        const u64 free_heap = heapGetFreeSize();
        constexpr u64 kKeepFree = 1536ULL << 20;
        const std::size_t max_budget =
            free_heap > kKeepFree ? static_cast<std::size_t>(free_heap - kKeepFree) : 0;
        std::size_t budget = physical_budget;
        if (budget > max_budget) {
            budget = max_budget;
        }
        budget &= ~(chunk_size - 1);
        if (budget >= chunk_size * 4) {
            virtmemLock();
            void* sv = virtmemFindStack(virtual_size, 0x200000);
            VirtmemReservation* rv = sv ? virtmemAddReservation(sv, virtual_size) : nullptr;
            virtmemUnlock();
            u8* pool = (sv && rv) ? static_cast<u8*>(memalign(0x1000, budget)) : nullptr;
            auto* mslot = (sv && rv && pool)
                              ? static_cast<std::uint32_t*>(std::calloc(num_chunks, sizeof(std::uint32_t)))
                              : nullptr;
            if (sv && rv && pool && mslot) {
                slot->mode = SparseMode::StackMap;
                slot->vbase = reinterpret_cast<std::uintptr_t>(sv);
                slot->vsize = virtual_size;
                slot->chunk_size = chunk_size;
                slot->num_chunks = num_chunks;
                slot->mapped = mapped;
                slot->mapped_slot = mslot;
                slot->pool_base = pool;
                slot->pool_chunks = budget / chunk_size;
                slot->pool_used = 0;
                slot->mapped_chunks = 0;
                slot->reservation = rv;
                std::snprintf(slot->tag, sizeof(slot->tag), "%s", tag ? tag : "sparse");
                slot->active.store(true, std::memory_order_release);
                mutexUnlock(&g_sparse_mgmt_mutex);
                return sv;
            }
            if (rv) {
                virtmemLock();
                virtmemRemoveReservation(rv);
                virtmemUnlock();
            }
            std::free(pool);
            std::free(mslot);
        }
    }

    std::free(mapped);
    mutexUnlock(&g_sparse_mgmt_mutex);
    return nullptr;
}

bool Switch2SparseCommitRange(void* addr, std::size_t size) {
    if (addr == nullptr || size == 0) {
        return true;
    }
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(addr);
    const std::uintptr_t end = start + size;
    for (std::size_t i = 0; i < kMaxSparseRegions; ++i) {
        auto& r = g_sparse_regions[i];
        if (!r.active.load(std::memory_order_acquire)) {
            continue;
        }
        if (start >= r.vbase + r.vsize || end <= r.vbase) {
            continue;
        }
        const std::uintptr_t clamped_start = start > r.vbase ? start : r.vbase;
        const std::uintptr_t clamped_end = end < (r.vbase + r.vsize) ? end : (r.vbase + r.vsize);
        const std::size_t first_chunk = (clamped_start - r.vbase) / r.chunk_size;
        const std::size_t last_chunk = (clamped_end - 1 - r.vbase) / r.chunk_size;

        // Lock-free fast path: check if all covered chunks are already committed.
        bool all_committed = true;
        for (std::size_t c = first_chunk; c <= last_chunk; ++c) {
            if (r.mapped[c].load(std::memory_order_acquire) == 0) {
                all_committed = false;
                break;
            }
        }
        if (all_committed) {
            return true;
        }

        mutexLock(&g_sparse_mgmt_mutex);
        bool ok = true;
        for (std::size_t c = first_chunk; c <= last_chunk; ++c) {
            if (r.mapped[c].load(std::memory_order_acquire) != 0) {
                continue;
            }
            void* const dst = reinterpret_cast<void*>(r.vbase + c * r.chunk_size);
            if (r.mode == SparseMode::AliasPhys) {
                const Result rc = svcMapPhysicalMemory(dst, r.chunk_size);
                if (R_FAILED(rc)) {
                    if (std::FILE* f = std::fopen("sdmc:/switch/eden-nx2/sparse_oom.txt", "a")) {
                        std::fprintf(f,
                                     "OOM tag=%s chunk=%zu/%zu mapped=%zu (%zu MiB) rc=0x%x\n",
                                     r.tag, c, r.num_chunks, r.mapped_chunks,
                                     (r.mapped_chunks * r.chunk_size) >> 20,
                                     static_cast<unsigned int>(rc));
                        std::fflush(f);
                        std::fclose(f);
                    }
                    ok = false;
                    break;
                }
                r.mapped[c].store(1, std::memory_order_release);
                ++r.mapped_chunks;
            } else if (r.mode == SparseMode::StackMap) {
                if (r.pool_used >= r.pool_chunks) {
                    ok = false;
                    break;
                }
                const std::size_t slot = r.pool_used++;
                void* const src = r.pool_base + slot * r.chunk_size;
                std::memset(src, 0, r.chunk_size);
                if (R_FAILED(svcMapMemory(dst, src, r.chunk_size))) {
                    --r.pool_used;
                    ok = false;
                    break;
                }
                r.mapped_slot[c] = static_cast<std::uint32_t>(slot + 1);
                r.mapped[c].store(1, std::memory_order_release);
                ++r.mapped_chunks;
            }
        }
        mutexUnlock(&g_sparse_mgmt_mutex);
        return ok;
    }
    // Not a sparse region (normal heap buffer) — already backed.
    return true;
}

bool Switch2SparseFree(void* base) {
    if (base == nullptr) {
        return false;
    }
    const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(base);
    mutexLock(&g_sparse_mgmt_mutex);
    for (std::size_t i = 0; i < kMaxSparseRegions; ++i) {
        auto& r = g_sparse_regions[i];
        if (!r.active.load(std::memory_order_acquire) || r.vbase != addr) {
            continue;
        }
        r.active.store(false, std::memory_order_release);

        for (std::size_t c = 0; c < r.num_chunks; ++c) {
            if (r.mapped[c].load(std::memory_order_acquire) == 0) {
                continue;
            }
            void* const dst = reinterpret_cast<void*>(r.vbase + c * r.chunk_size);
            if (r.mode == SparseMode::AliasPhys) {
                svcUnmapPhysicalMemory(dst, r.chunk_size);
            } else if (r.mode == SparseMode::StackMap && r.mapped_slot != nullptr &&
                       r.mapped_slot[c] != 0) {
                void* const src = r.pool_base + static_cast<std::size_t>(r.mapped_slot[c] - 1) * r.chunk_size;
                svcUnmapMemory(dst, src, r.chunk_size);
            }
        }
        if (r.reservation != nullptr) {
            virtmemLock();
            virtmemRemoveReservation(r.reservation);
            virtmemUnlock();
            r.reservation = nullptr;
        }
        std::free(r.pool_base);
        r.pool_base = nullptr;
        std::free(r.mapped_slot);
        r.mapped_slot = nullptr;
        std::free(r.mapped);
        r.mapped = nullptr;
        r.vbase = 0;
        r.vsize = 0;
        r.mode = SparseMode::None;
        mutexUnlock(&g_sparse_mgmt_mutex);
        return true;
    }
    mutexUnlock(&g_sparse_mgmt_mutex);
    return false;
}

bool Switch2SparseClearZero(void* addr, std::size_t size) {
    if (addr == nullptr || size == 0) {
        return false;
    }
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(addr);
    const std::uintptr_t end = start + size;
    for (std::size_t i = 0; i < kMaxSparseRegions; ++i) {
        auto& r = g_sparse_regions[i];
        if (!r.active.load(std::memory_order_acquire)) {
            continue;
        }
        if (start < r.vbase || end > r.vbase + r.vsize) {
            continue;
        }
        const std::size_t first_chunk = (start - r.vbase) / r.chunk_size;
        const std::size_t last_chunk = (end - 1 - r.vbase) / r.chunk_size;
        for (std::size_t c = first_chunk; c <= last_chunk; ++c) {
            if (r.mapped[c].load(std::memory_order_acquire) == 0) {
                continue;
            }
            const std::uintptr_t cs = r.vbase + c * r.chunk_size;
            const std::uintptr_t ce = cs + r.chunk_size;
            const std::uintptr_t zs = start > cs ? start : cs;
            const std::uintptr_t ze = end < ce ? end : ce;
            if (ze > zs) {
                std::memset(reinterpret_cast<void*>(zs), 0, ze - zs);
            }
        }
        return true;
    }
    return false;
}

bool Switch2IsSparseUnmapped(const void* addr) {
    if (addr == nullptr) {
        return false;
    }
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(addr);
    for (std::size_t i = 0; i < kMaxSparseRegions; ++i) {
        const auto& r = g_sparse_regions[i];
        if (!r.active.load(std::memory_order_acquire)) {
            continue;
        }
        if (a < r.vbase || a >= r.vbase + r.vsize) {
            continue;
        }
        const std::size_t idx = (a - r.vbase) / r.chunk_size;
        return r.mapped[idx].load(std::memory_order_acquire) == 0;
    }
    return false;
}

bool Switch2SparseCanBackSize(std::size_t virtual_size) {
    mutexLock(&g_sparse_mgmt_mutex);
    Switch2SparseInitUnlocked();
    const bool ok = (g_sparse_alias_phys_ok && g_sparse_alias_size >= virtual_size + (256ULL << 20)) ||
                    (g_sparse_stack_map_ok && g_sparse_stack_size >= virtual_size + (256ULL << 20));
    mutexUnlock(&g_sparse_mgmt_mutex);
    return ok;
}

const char* Switch2SparseGetReport(void) {
    mutexLock(&g_sparse_mgmt_mutex);
    Switch2SparseInitUnlocked();
    mutexUnlock(&g_sparse_mgmt_mutex);
    return g_sparse_report;
}

} // extern "C"

#endif

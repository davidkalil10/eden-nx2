// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Isolated libnx runtime hooks for Switch 2 so <switch.h> never collides with Eden's
// global `Service` namespace or `u128` type.

#ifdef __SWITCH2__

#include <cstdint>
#include <cstdio>
#include <cstring>

#define u128 libnx_u128
#define s128 libnx_s128
#include <switch.h>
#include <switch2/memory/heap.h>
#undef u128
#undef s128

#include "yuzu_cmd/switch2_runtime.h"

namespace {

PadState g_pad{};
bool g_pad_initialized = false;

struct Switch2SensorAndMotorState {
    HidNpadIdType id{};
    u32 style{};
    bool valid{};
    HidSixAxisSensorHandle sensors[2]{};
    int sensor_count{};
    HidVibrationDeviceHandle motors[2]{};
    bool has_motors{};
    u64 last_motion_tick{};
};

Switch2SensorAndMotorState g_pad_dev{};
int g_plus_minus_hold_frames = 0;

Framebuffer g_fb{};
bool g_fb_initialized = false;

void AcquireSensorsAndMotors(HidNpadIdType id, u32 style_set) {
    for (int i = 0; i < g_pad_dev.sensor_count; ++i) {
        hidStopSixAxisSensor(g_pad_dev.sensors[i]);
    }
    g_pad_dev = {};
    g_pad_dev.id = id;
    g_pad_dev.style = style_set;
    g_pad_dev.valid = true;

    HidNpadStyleTag style{};
    int sensor_count = 1;
    if (style_set & HidNpadStyleTag_NpadHandheld) {
        style = HidNpadStyleTag_NpadHandheld;
    } else if (style_set & HidNpadStyleTag_NpadFullKey) {
        style = HidNpadStyleTag_NpadFullKey;
    } else if (style_set & HidNpadStyleTag_NpadJoyDual) {
        style = HidNpadStyleTag_NpadJoyDual;
        sensor_count = 2;
    } else {
        return;
    }

    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(g_pad_dev.sensors, sensor_count, id, style))) {
        g_pad_dev.sensor_count = sensor_count;
        for (int i = 0; i < sensor_count; ++i) {
            hidStartSixAxisSensor(g_pad_dev.sensors[i]);
        }
    }
    g_pad_dev.has_motors =
        R_SUCCEEDED(hidInitializeVibrationDevices(g_pad_dev.motors, 2, id, style));
}

} // namespace

extern "C" {

extern char __start__[];

struct __pthread_t;
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
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&g_pad);
        hidInitializeTouchScreen();
        g_pad_dev = {};
        g_pad_initialized = true;
    }
    g_plus_minus_hold_frames = 0;
}

bool Switch2PollInput(Switch2PadSnapshot* out, bool allow_plus_minus_exit) {
    if (out != nullptr) {
        std::memset(out, 0, sizeof(*out));
    }
    if (!appletMainLoop()) {
        return false;
    }
    if (!g_pad_initialized) {
        Switch2InitInput();
    }

    padUpdate(&g_pad);

    const HidNpadIdType id = padIsHandheld(&g_pad) ? HidNpadIdType_Handheld : HidNpadIdType_No1;
    const u32 style_set = padGetStyleSet(&g_pad);
    if (!g_pad_dev.valid || g_pad_dev.id != id || g_pad_dev.style != style_set) {
        AcquireSensorsAndMotors(id, style_set);
    }

    const u64 held = padGetButtons(&g_pad);
    const u64 down = padGetButtonsDown(&g_pad);

    if (allow_plus_minus_exit && (held & HidNpadButton_Plus) && (held & HidNpadButton_Minus)) {
        if (++g_plus_minus_hold_frames >= 30) {
            return false;
        }
    } else {
        g_plus_minus_hold_frames = 0;
    }

    if (out == nullptr) {
        return true;
    }

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

    const HidAnalogStickState left = padGetStickPos(&g_pad, 0);
    const HidAnalogStickState right = padGetStickPos(&g_pad, 1);
    constexpr float kStickMax = static_cast<float>(JOYSTICK_MAX);
    out->left_x = static_cast<float>(left.x) / kStickMax;
    out->left_y = static_cast<float>(left.y) / kStickMax;
    out->right_x = static_cast<float>(right.x) / kStickMax;
    out->right_y = static_cast<float>(right.y) / kStickMax;

    // Virtual stick navigation bits (matching D-Pad style for launcher navigation)
    if (down & HidNpadButton_StickLLeft)  out->raw_down |= SWITCH2_BTN_LEFT;
    if (down & HidNpadButton_StickLUp)    out->raw_down |= SWITCH2_BTN_UP;
    if (down & HidNpadButton_StickLRight) out->raw_down |= SWITCH2_BTN_RIGHT;
    if (down & HidNpadButton_StickLDown)  out->raw_down |= SWITCH2_BTN_DOWN;

    HidSixAxisSensorState motion{};
    if (g_pad_dev.sensor_count > 0 &&
        hidGetSixAxisSensorStates(g_pad_dev.sensors[g_pad_dev.sensor_count - 1], &motion, 1) > 0) {
        const u64 tick = armGetSystemTick();
        out->has_motion = true;
        out->motion_delta_us =
            g_pad_dev.last_motion_tick != 0 ? armTicksToNs(tick - g_pad_dev.last_motion_tick) / 1000 : 0;
        g_pad_dev.last_motion_tick = tick;
        out->gyro[0] = motion.angular_velocity.x;
        out->gyro[1] = motion.angular_velocity.y;
        out->gyro[2] = motion.angular_velocity.z;
        out->accel[0] = motion.acceleration.x;
        out->accel[1] = motion.acceleration.y;
        out->accel[2] = motion.acceleration.z;
    }

    HidTouchScreenState touch{};
    if (hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0) {
        out->has_touch = true;
        out->touch_x = static_cast<float>(touch.touches[0].x) / 1280.0f;
        out->touch_y = static_cast<float>(touch.touches[0].y) / 720.0f;
    }

    return true;
}

void Switch2SetVibration(float l_low_amp, float l_low_freq, float l_high_amp, float l_high_freq,
                         float r_low_amp, float r_low_freq, float r_high_amp, float r_high_freq) {
    if (!g_pad_dev.has_motors) {
        return;
    }
    const HidVibrationValue values[2]{
        {l_low_amp, l_low_freq, l_high_amp, l_high_freq},
        {r_low_amp, r_low_freq, r_high_amp, r_high_freq},
    };
    hidSendVibrationValues(g_pad_dev.motors, values, 2);
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

} // extern "C"

#endif

// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __SWITCH2__

#include <cstdint>

extern "C" {

enum Switch2ButtonBits : std::uint64_t {
    SWITCH2_BTN_A      = 1ULL << 0,
    SWITCH2_BTN_B      = 1ULL << 1,
    SWITCH2_BTN_X      = 1ULL << 2,
    SWITCH2_BTN_Y      = 1ULL << 3,
    SWITCH2_BTN_LSTICK = 1ULL << 4,
    SWITCH2_BTN_RSTICK = 1ULL << 5,
    SWITCH2_BTN_L      = 1ULL << 6,
    SWITCH2_BTN_R      = 1ULL << 7,
    SWITCH2_BTN_ZL     = 1ULL << 8,
    SWITCH2_BTN_ZR     = 1ULL << 9,
    SWITCH2_BTN_PLUS   = 1ULL << 10,
    SWITCH2_BTN_MINUS  = 1ULL << 11,
    SWITCH2_BTN_LEFT   = 1ULL << 12,
    SWITCH2_BTN_UP     = 1ULL << 13,
    SWITCH2_BTN_RIGHT  = 1ULL << 14,
    SWITCH2_BTN_DOWN   = 1ULL << 15,
    SWITCH2_BTN_SL     = 1ULL << 16,
    SWITCH2_BTN_SR     = 1ULL << 17,
};

struct Switch2PadSnapshot {
    std::uint32_t buttons;
    std::uint64_t raw_down;
    std::uint64_t raw_held;
    float left_x;
    float left_y;
    float right_x;
    float right_y;
    bool has_motion;
    std::uint64_t motion_delta_us;
    float gyro[3];
    float accel[3];
    bool has_touch;
    float touch_x;
    float touch_y;
};

void* Switch2GetNativeWindow(void);
void Switch2GetWindowDimensions(std::uint32_t* w, std::uint32_t* h);
void Switch2SleepMs(std::uint64_t ms);
bool Switch2MainLoop(void);

void Switch2InitInput(void);
bool Switch2PollInput(Switch2PadSnapshot* out, bool allow_plus_minus_exit);
void Switch2SetVibration(float l_low_amp, float l_low_freq, float l_high_amp, float l_high_freq,
                         float r_low_amp, float r_low_freq, float r_high_amp, float r_high_freq);
void Switch2SetCpuBoost(bool enable);
void Switch2GetMemoryStats(std::uint64_t* out_used, std::uint64_t* out_total);
std::uint64_t Switch2GetCoreMask(void);

bool Switch2FbInit(void);
std::uint32_t* Switch2FbBegin(std::uint32_t* out_stride_pixels);
void Switch2FbEnd(void);
void Switch2FbClose(void);

} // extern "C"

#endif // __SWITCH2__

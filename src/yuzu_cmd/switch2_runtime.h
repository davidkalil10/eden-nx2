// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __SWITCH2__

#include <cstddef>
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

enum Switch2PhysicalStyle : std::uint8_t {
    SWITCH2_STYLE_NONE = 0,
    SWITCH2_STYLE_HANDHELD = 1,
    SWITCH2_STYLE_PRO = 2,
    SWITCH2_STYLE_JOY_DUAL = 3,
    SWITCH2_STYLE_JOY_LEFT = 4,
    SWITCH2_STYLE_JOY_RIGHT = 5,
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
    bool connected;
    std::uint8_t physical_style;
};

void* Switch2GetNativeWindow(void);
void Switch2GetWindowDimensions(std::uint32_t* w, std::uint32_t* h);
void Switch2SleepMs(std::uint64_t ms);
bool Switch2MainLoop(void);

void Switch2InitInput(void);
bool Switch2PollInput(Switch2PadSnapshot* out, bool allow_plus_minus_exit);
void Switch2PollPlayerInput(int player_idx, Switch2PadSnapshot* out);
void Switch2SetVibration(float l_low_amp, float l_low_freq, float l_high_amp, float l_high_freq,
                         float r_low_amp, float r_low_freq, float r_high_amp, float r_high_freq);
void Switch2SetPlayerVibration(int player_idx, float l_low_amp, float l_low_freq, float l_high_amp,
                               float l_high_freq, float r_low_amp, float r_low_freq,
                               float r_high_amp, float r_high_freq);
void Switch2SetCpuBoost(bool enable);
void Switch2GetMemoryStats(std::uint64_t* out_used, std::uint64_t* out_total);
std::uint64_t Switch2GetCoreMask(void);
std::uint64_t Switch2GetSystemTickFreq(void);

void* Switch2SparseAlloc(std::size_t virtual_size, std::size_t chunk_size,
                         std::size_t physical_budget, const char* tag);
bool Switch2SparseFree(void* base);
bool Switch2SparseCommitRange(void* addr, std::size_t size);
bool Switch2SparseClearZero(void* addr, std::size_t size);
bool Switch2IsSparseUnmapped(const void* addr);
bool Switch2SparseCanBackSize(std::size_t virtual_size);
const char* Switch2SparseGetReport(void);

enum Switch2KbdStatus : int {
    SWITCH2_KBD_OK = 0,
    SWITCH2_KBD_CANCEL = 1,
    SWITCH2_KBD_UNAVAILABLE = 2,
};

struct Switch2KbdRequest {
    const char* header_text;
    const char* sub_text;
    const char* guide_text;
    const char* ok_text;
    const char* initial_text;
    int max_len;
    int kbd_type; // 0=Normal, 1=NumPad, 2=QWERTY
    bool password_mode;
};

int Switch2ShowNativeKeyboard(const Switch2KbdRequest* req, char* out_utf8, int out_utf8_size);

bool Switch2FbInit(void);
std::uint32_t* Switch2FbBegin(std::uint32_t* out_stride_pixels);
void Switch2FbEnd(void);
void Switch2FbClose(void);
void Switch2ScheduleReturnToLauncher(const char* argv0);

} // extern "C"

#endif // __SWITCH2__

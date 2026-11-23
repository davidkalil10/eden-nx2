// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __SWITCH2__

#include <string>

enum class Switch2ControllerProfile {
    ProController,
    Auto,
    Handheld,
    DualJoycon,
    RightJoycon,
    LeftJoycon,
};

enum class Switch2PlayerSlotMode {
    Auto,
    Off,
    ProController,
    DualJoycon,
    RightJoycon,
    LeftJoycon,
};

struct Switch2LaunchConfig {
    std::string rom_path;
    Switch2ControllerProfile controller_profile = Switch2ControllerProfile::ProController;
    Switch2PlayerSlotMode controller_p2 = Switch2PlayerSlotMode::Auto;
    Switch2PlayerSlotMode controller_p34 = Switch2PlayerSlotMode::Auto;
    bool docked_mode = false;
    float resolution_scale = 1.0f;
    int scaling_filter = 0; // 0=Bilinear, 1=FSR, 2=SGSR, 3=ScaleForce, 4=MMPX, 5=Lanczos, 6=Nearest
    int anti_aliasing = 0;  // 0=None, 1=FXAA, 2=SMAA
    bool gpu_fast = true;
    bool cpu_unsafe = true;
    int frame_gen_mult = 0; // 0 = Off, 2..4 = Lossless Scaling multiplier
    int show_fps_mode = 1;  // 0 = Off, 1 = FPS + Speed, 2 = Detailed
    int vsync_mode = 0;     // 0 = Relaxed, 1 = Off (Immediate), 2 = Mailbox, 3 = FIFO
    int speed_limit = 100;  // 0 = Unlocked, 100 / 150 / 200 (%)
    bool cpu_boost = false;
    int address_space_mode = 1; // 0 = 36-bit compact, 1 = 38-bit balanced, 2 = 39-bit full
    int jit_blocks_ahead = 8;
    bool shader_cache = true;
    std::string language = "pt-BR";
    std::string profile_name = "Eden";
    bool kbd_force_overlay = false;
    bool ui_english = false;
    std::string update_pkg = "auto"; // "auto", "none", or full path to Update .nsp/.xci
    std::string dlc_pkg = "auto";    // "auto", "none", or full path to DLC .nsp/.xci or folder
    bool request_restart = false;    // Set when auto-updater installs a new NRO and asks to relaunch
};

// Opens the 1280x720 graphical ROM selector & settings dashboard.
// Returns true when a game is chosen (fills out_cfg); returns false to exit/restart.
bool Switch2RunLauncher(Switch2LaunchConfig* out_cfg, const char* self_nro_path = nullptr);

// Saves live-editable settings from the in-game quick menu into this game's per-game .ini.
bool Switch2SavePerGameQuickConfig(const Switch2LaunchConfig& cfg);

// Draws the in-game Quick Menu (L+R+(+)) into Common::GetOverlayPanels()[2] (Center anchor).
void Switch2RenderQuickMenuOverlay(const Switch2LaunchConfig& cfg, int volume_pct, int selected,
                                   const std::string& status_msg);
void Switch2HideQuickMenuOverlay();

// Built-in QWERTY Virtual Keyboard overlay (automatic fallback or forced mode for swkbd).
char Switch2KeyboardCharAt(int row, int col, bool shift, bool symbols);
void Switch2RenderKeyboardOverlay(const std::string& header, const std::string& text, int max_len,
                                  int row, int col, bool shift, bool symbols, bool ui_english);
void Switch2HideKeyboardOverlay();

#endif // __SWITCH2__

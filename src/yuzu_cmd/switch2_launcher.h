// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __SWITCH2__

#include <string>

enum class Switch2ControllerProfile {
    ProController,
    Handheld,
    DualJoycon,
    RightJoycon,
    LeftJoycon,
};

struct Switch2LaunchConfig {
    std::string rom_path;
    Switch2ControllerProfile controller_profile = Switch2ControllerProfile::ProController;
    bool docked_mode = false;
    float resolution_scale = 1.0f;
    bool gpu_fast = true;
    bool cpu_boost = true;
    int address_space_mode = 1; // 0 = 36-bit compact, 1 = 38-bit balanced, 2 = 39-bit full
    int jit_blocks_ahead = 8;
    bool shader_cache = true;
    std::string language = "pt-BR";
};

// Opens the 1280x720 graphical ROM selector & settings dashboard.
// Returns true when a game is chosen (fills out_cfg); returns false to exit cleanly.
bool Switch2RunLauncher(Switch2LaunchConfig* out_cfg);

#endif // __SWITCH2__

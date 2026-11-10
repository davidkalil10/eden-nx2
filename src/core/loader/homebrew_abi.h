// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

namespace Loader {

/// Matches libnx AppletType / EnvAppletFlags for HBABI EntryType_AppletType.
enum class HomebrewAbiAppletMode : u32 {
    /// nx-hbloader default when launched from album / hbl (AppletType_LibraryApplet).
    AtmosphereHblChild = 0,
    /// Standalone homebrew NRO launched as the foreground application (SystemApplication + override).
    ForegroundApplication = 1,
};

struct HomebrewAbiOptions {
    HomebrewAbiAppletMode applet_mode = HomebrewAbiAppletMode::ForegroundApplication;
    u64 last_load_result = 0;
};

} // namespace Loader

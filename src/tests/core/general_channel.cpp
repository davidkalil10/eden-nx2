// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"

// Each session gets a general channel event from its own kernel: one kept from the previous
// session is destroyed, and the next qlaunch waiting on it fails with 2001-0114.
TEST_CASE("System::ShutdownMainProcess leaves no general channel to the next session", "[core]") {
    Core::System system;
    system.Initialize();
    system.Kernel().Initialize();

    system.PushGeneralChannelData({1, 2, 3});
    system.ShutdownMainProcess();

    system.Kernel().Initialize();
    std::vector<u8> data;
    REQUIRE_FALSE(system.TryPopGeneralChannel(data));

    // The new session's channel works, with an event of its own kernel.
    system.PushGeneralChannelData({4});
    REQUIRE(system.TryPopGeneralChannel(data));
    REQUIRE(data == std::vector<u8>{4});

    system.ShutdownMainProcess();
}

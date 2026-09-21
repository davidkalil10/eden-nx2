// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/core.h"
#include "core/hle/kernel/kernel.h"

namespace TestKernel {

/**
 * The kernel this test binary runs against.
 *
 * Enough of one for anything that needs kernel objects: events, processes, the service context
 * helpers, and the asynchronous contexts built on them. No ROM, no firmware, no keys, and no
 * game is loaded.
 *
 * Shared, and brought up once on first use: each KernelCore Initialize/Shutdown cycle keeps
 * about 33 MB for the life of the process, so one kernel per test would reach a gigabyte.
 *
 * Tests share it, so do not leave kernel objects behind. Close what you open.
 *
 * What it does not reach: the service classes themselves. Most live inside their own .cpp with
 * no header, so a test cannot name them, and invoking a command needs a thread and a session
 * this does not build. Anything reachable from a public header works.
 */
[[nodiscard]] Core::System& Shared();

} // namespace TestKernel

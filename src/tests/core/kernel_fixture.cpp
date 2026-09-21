// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/core/kernel_fixture.h"

namespace TestKernel {

namespace {

/// Owns the shared kernel and takes it down at exit.
///
/// The shutdown is the part that is easy to miss. A System that unwinds without it destroys the
/// kernel's object container while entries are still in it, walks an intrusive tree into
/// whatever the stack held, and crashes several frames deep in boost, which reads like the
/// bring-up failed rather than the teardown.
class Instance {
public:
    Instance() {
        system.Initialize();
        system.Kernel().Initialize();
    }

    ~Instance() {
        system.Kernel().Shutdown();
    }

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    Core::System system;
};

} // Anonymous namespace

Core::System& Shared() {
    static Instance instance;
    return instance.system;
}

} // namespace TestKernel

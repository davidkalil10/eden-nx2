// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/svc_types.h"
#include "core/hle/service/am/process_holder.h"
#include "tests/core/kernel_fixture.h"

// Winding restarts an applet in a new process and can close the old one before AM's event
// observer has handled its end, so the holder keeps the process it waits on open until it goes.
TEST_CASE("Service::AM a process holder keeps the process it waits on open", "[core][kernel]") {
    auto& kernel = TestKernel::Shared().Kernel();

    // A bare process, made the way KernelCore::RunOnHostCoreProcess makes one.
    auto* const process = Kernel::KProcess::Create(kernel);
    REQUIRE(R_SUCCEEDED(process->Initialize(kernel, Kernel::Svc::CreateProcessParameter{},
                                            kernel.GetSystemResourceLimit(), false)));
    Kernel::KProcess::Register(kernel, process);
    const u32 before = process->GetReferenceCount();

    {
        Service::AM::ProcessHolder holder{kernel, process, nullptr};
        CHECK(holder.GetWatchedProcess() == process);
        CHECK(process->GetReferenceCount() == before + 1);
    }

    CHECK(process->GetReferenceCount() == before);
    process->Close(kernel);
}

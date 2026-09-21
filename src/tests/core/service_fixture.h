// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include <vector>

#include "common/common_types.h"

namespace Kernel {
class KernelCore;
class KProcess;
} // namespace Kernel

namespace TestKernel {

/**
 * A guest process with a heap, built without a ROM.
 *
 * An HLE handler reads and writes the buffers a request names through the calling process's own
 * page table, so anything that hands a service a buffer address needs a process with mapped
 * pages. Neither process the kernel already has will do: the ones behind
 * KernelHelpers::ServiceContext and RunOnHostCoreProcess are made with the KProcess::Initialize
 * overload that builds no page table, so every address in them is unmapped.
 *
 * LoadFromMetadata is the overload that does build one, and ProgramMetadata::GetDefault()
 * describes a 64-bit application without reading a file, which is what the loaders pass for an
 * NRO. What it leaves out is the part KProcess::Run does, and Run is private: the handle table
 * has to be initialized here or a reply carrying a handle writes into a table with no entries.
 *
 * Expensive to build, because LoadFromMetadata also creates four Dynarmic interfaces
 * (KProcess::InitializeInterfaces). One per suite, like the kernel it lives in.
 */
class GuestProcess {
public:
    /// An address and a length inside the heap: what a buffer descriptor is built from.
    struct Region {
        u64 address;
        u64 size;
    };

    explicit GuestProcess(Kernel::KernelCore& kernel_, u64 heap_size);
    ~GuestProcess();

    GuestProcess(const GuestProcess&) = delete;
    GuestProcess& operator=(const GuestProcess&) = delete;

    [[nodiscard]] Kernel::KProcess& Process() const {
        return *process;
    }

    /// Hands out @p size zeroed bytes of heap nothing else here is using.
    [[nodiscard]] Region Reserve(u64 size);

    /// Reserve plus a write, for the input side of a request.
    Region Write(std::span<const u8> bytes);

    void Write(u64 address, std::span<const u8> bytes);

    [[nodiscard]] std::vector<u8> Read(u64 address, u64 size) const;

private:
    Kernel::KernelCore& kernel;
    Kernel::KProcess* process{};
    u64 heap_base{};
    u64 heap_end{};
    u64 next_free{};
};

} // namespace TestKernel

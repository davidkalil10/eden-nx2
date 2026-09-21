// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdexcept>

#include <cstdio>
#include <format>

#include "common/alignment.h"
#include "common/assert.h"
#include "core/file_sys/program_metadata.h"
#include "core/hle/kernel/k_handle_table.h"
#include "core/hle/result.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_process_page_table.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/service_fixture.h"

namespace TestKernel {

namespace {

/// One page, so the address space has a code region to lay out around. Nothing is loaded into it.
constexpr std::size_t CodeSize = 0x1000;

/// Buffer descriptors carry byte addresses, but a service that reads a struct out of one wants it
/// aligned the way the guest's allocator would have left it.
constexpr u64 ReservationAlignment = 16;

/// Throws rather than asserts, because the kernel's ASSERT only logs and carries on, and the test
/// binary starts no logging backend: a failed set-up would otherwise show up as an unexplained
/// zero address several frames later.
void Demand(Result result, std::string_view what) {
    if (result.IsError()) {
        throw std::runtime_error{std::format("{} failed with {:#010x}", what, result.raw)};
    }
}

} // Anonymous namespace

GuestProcess::GuestProcess(Kernel::KernelCore& kernel_, u64 heap_size) : kernel{kernel_} {
    process = Kernel::KProcess::Create(kernel);
    if (process == nullptr) {
        throw std::runtime_error{"KProcess::Create returned nothing"};
    }
    Kernel::KProcess::Register(kernel, process);

    Demand(process->LoadFromMetadata(kernel, FileSys::ProgramMetadata::GetDefault(), CodeSize, 0,
                                     0),
           "LoadFromMetadata");

    // The next two are the part of KProcess::Run this process will never reach, Run being private.
    // Without the handle table a reply carrying a handle writes into a table with no entries;
    // without a maximum, SetHeapSize refuses every size with ResultOutOfMemory, because the
    // maximum starts at zero and only Run derives one from the process memory limit.
    Demand(process->GetHandleTable().Initialize(
               kernel, static_cast<s32>(Kernel::KHandleTable::MaxTableSize)),
           "KHandleTable::Initialize");
    Demand(process->GetPageTable().SetMaxHeapSize(heap_size), "SetMaxHeapSize");

    Common::ProcessAddress address{};
    Demand(process->GetPageTable().SetHeapSize(std::addressof(address), heap_size), "SetHeapSize");

    heap_base = GetInteger(address);
    heap_end = heap_base + heap_size;
    next_free = heap_base;
}

GuestProcess::~GuestProcess() {
    // KProcess::Finalize only reaches the handle table through Exit(), and this process never ran,
    // so anything a reply left in it would keep its object alive past the kernel's shutdown check.
    process->GetHandleTable().Finalize(kernel);
    process->Close(kernel);
}

GuestProcess::Region GuestProcess::Reserve(u64 size) {
    const u64 address = Common::AlignUp(next_free, ReservationAlignment);
    next_free = address + size;
    ASSERT_MSG(next_free <= heap_end, "the guest heap is too small for this suite");

    // SetHeapSize does not promise zeroed pages, and a test that reads back what a handler wrote
    // has to be able to tell a short write from a previous test's leftovers.
    process->GetMemory().ZeroBlock(address, size);

    return Region{address, size};
}

GuestProcess::Region GuestProcess::Write(std::span<const u8> bytes) {
    const Region region = Reserve(bytes.size());
    Write(region.address, bytes);
    return region;
}

void GuestProcess::Write(u64 address, std::span<const u8> bytes) {
    process->GetMemory().WriteBlock(address, bytes.data(), bytes.size());
}

std::vector<u8> GuestProcess::Read(u64 address, u64 size) const {
    std::vector<u8> bytes(size);
    process->GetMemory().ReadBlock(address, bytes.data(), bytes.size());
    return bytes;
}

} // namespace TestKernel

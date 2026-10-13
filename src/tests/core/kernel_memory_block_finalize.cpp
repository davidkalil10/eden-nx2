// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/hle/kernel/k_memory_block_manager.h"
#include "core/hle/kernel/k_system_resource.h"
#include "core/hle/kernel/kernel.h"
#include "tests/core/kernel_fixture.h"

// With NCE the host buffer reserves 2^38 bytes while a 39-bit process runs to 2^39, so the free
// block at the top must not be handed back for unmapping.
TEST_CASE("Kernel::KMemoryBlockManager finalize skips the free blocks", "[core][kernel]") {
    auto& kernel = TestKernel::Shared().Kernel();
    auto* const slab_manager = kernel.GetSystemSystemResource().GetMemoryBlockSlabManagerPointer();

    constexpr u64 Start = 128ULL << 20;
    constexpr u64 End = 1ULL << 39;
    constexpr u64 Heap = 1ULL << 32;
    constexpr size_t HeapPages = 16;

    Kernel::KMemoryBlockManager manager;
    REQUIRE(R_SUCCEEDED(manager.Initialize(Start, End, slab_manager)));

    Result result = ResultSuccess;
    Kernel::KMemoryBlockManagerUpdateAllocator allocator{std::addressof(result), slab_manager};
    REQUIRE(R_SUCCEEDED(result));
    manager.Update(std::addressof(allocator), Heap, HeapPages, Kernel::KMemoryState::Normal,
                   Kernel::KMemoryPermission::UserReadWrite, Kernel::KMemoryAttribute::None,
                   Kernel::KMemoryBlockDisableMergeAttribute::Normal,
                   Kernel::KMemoryBlockDisableMergeAttribute::None);

    std::vector<std::pair<u64, u64>> handed_back;
    manager.Finalize(slab_manager, [&](Common::ProcessAddress address, u64 size) {
        handed_back.emplace_back(GetInteger(address), size);
    });

    REQUIRE(handed_back.size() == 1);
    CHECK(handed_back[0].first == Heap);
    CHECK(handed_back[0].second == HeapPages * Kernel::PageSize);
}

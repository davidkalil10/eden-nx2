// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "core/core.h"
#include "tests/core/kernel_fixture.h"
#include "tests/core/service_fixture.h"

namespace {

constexpr u64 HeapSize = 2 * 1024 * 1024;

[[nodiscard]] std::span<const u8> AsBytes(const auto& value) {
    return std::span<const u8>{reinterpret_cast<const u8*>(std::addressof(value)), sizeof(value)};
}

} // Anonymous namespace

// The whole HLE harness rests on this: a process built from the default metadata, with no ROM
// behind it, has a page table and a heap a service can be handed addresses into. If this fails
// every buffer test above it fails for a reason that has nothing to do with the service.
TEST_CASE("TestKernel::GuestProcess hands out heap a service can address", "[core][kernel]") {
    TestKernel::GuestProcess guest{TestKernel::Shared().Kernel(), HeapSize};

    const std::array<u64, 4> written{0x0123456789abcdef, 0, 0xfedcba9876543210, 42};
    const auto region = guest.Write(AsBytes(written));

    REQUIRE(region.address != 0);
    REQUIRE(region.size == sizeof(written));

    std::array<u64, 4> read{};
    const auto bytes = guest.Read(region.address, sizeof(read));
    REQUIRE(bytes.size() == sizeof(read));
    std::memcpy(read.data(), bytes.data(), bytes.size());

    REQUIRE(read == written);
}

// Two reservations that overlapped would let one test's buffer answer another's read, and the
// failure would land wherever the second test happens to look rather than here.
TEST_CASE("TestKernel::GuestProcess does not hand out the same heap twice", "[core][kernel]") {
    TestKernel::GuestProcess guest{TestKernel::Shared().Kernel(), HeapSize};

    const auto first = guest.Reserve(0x100);
    const auto second = guest.Reserve(0x100);

    REQUIRE(first.address + first.size <= second.address);

    const std::vector<u8> filler(first.size, 0xa5);
    guest.Write(first.address, filler);

    // Reserve zeroes what it hands out, so the neighbour is readable and untouched rather than
    // whatever the page happened to hold.
    REQUIRE(guest.Read(second.address, second.size) == std::vector<u8>(second.size, 0));
}

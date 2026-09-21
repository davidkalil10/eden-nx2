// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "core/core.h"
#include "core/hle/result.h"
#include "core/hle/service/hle_ipc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/service.h"
#include "tests/core/ipc_request.h"
#include "tests/core/kernel_fixture.h"
#include "tests/core/service_fixture.h"

namespace {

constexpr u64 HeapSize = 2 * 1024 * 1024;

[[nodiscard]] std::span<const u8> AsBytes(const auto& value) {
    return std::span<const u8>{reinterpret_cast<const u8*>(std::addressof(value)), sizeof(value)};
}

/// A service that exists only here, so the harness can be judged without a real one in the way:
/// a failure in these tests is the harness's, and every service test above them can take the
/// request and reply path for granted.
class ArithmeticService final : public Service::ServiceFramework<ArithmeticService> {
public:
    explicit ArithmeticService(Core::System& system_)
        : ServiceFramework{system_, "ArithmeticService"} {
        static const FunctionInfo functions[] = {
            {1, &ArithmeticService::Add, "Add"},
        };
        RegisterHandlers(functions);
    }

private:
    void Add(Service::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto left = rp.Pop<u32>();
        const auto right = rp.Pop<u64>();

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(left + right);
    }
};

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

// The request buffer this suite builds by hand has to be the one HLERequestContext expects, down
// to the padding: the command has to reach the right handler, its parameters have to arrive in
// the order they were pushed, and the reply has to be readable back out of the context.
TEST_CASE("TestKernel::ServiceHarness carries a command to a handler and the reply back",
          "[core][kernel]") {
    auto& harness = TestKernel::Harness();
    const auto service = harness.Open(
        std::make_shared<ArithmeticService>(TestKernel::Shared()), TestKernel::SessionKind::Plain);

    TestKernel::RequestBuilder request{1};
    request.PushRaw<u32>(7).PushRaw<u64>(35);

    auto reply = harness.Call(service, request);
    REQUIRE(reply.GetResult() == ResultSuccess);
    REQUIRE(reply.Pop<u64>() == 42);

    harness.Close(service);
}

// A domain request carries a header a plain one does not, and everything after it shifts. Any
// service a test reaches through an interface is addressed this way, so the layout is pinned
// here, on a handler that cannot be the thing at fault.
TEST_CASE("TestKernel::ServiceHarness addresses a domain object", "[core][kernel]") {
    auto& harness = TestKernel::Harness();
    const auto service = harness.Open(
        std::make_shared<ArithmeticService>(TestKernel::Shared()), TestKernel::SessionKind::Domain);

    TestKernel::RequestBuilder request{1};
    request.PushRaw<u32>(1).PushRaw<u64>(2);

    auto reply = harness.Call(service, request);
    REQUIRE(reply.GetResult() == ResultSuccess);
    REQUIRE(reply.Pop<u64>() == 3);

    // Nothing was handed back, so a domain reply says so rather than leaving the count wherever
    // the previous reply left it.
    REQUIRE(reply.DomainObjectCount() == 0);

    harness.Close(service);
}

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

/// Copies its send buffer into its receive buffer, which is the only thing a buffer test needs a
/// handler to do, and reports how much of it fit.
class EchoService final : public Service::ServiceFramework<EchoService> {
public:
    explicit EchoService(Core::System& system_) : ServiceFramework{system_, "EchoService"} {
        static const FunctionInfo functions[] = {
            {1, &EchoService::Echo, "Echo"},
        };
        RegisterHandlers(functions);
    }

private:
    void Echo(Service::HLERequestContext& ctx) {
        const auto input = ctx.ReadBuffer();
        const auto written = ctx.WriteBuffer(input.data(), input.size());

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(written);
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

// The HLE receive path never remaps anything: it reads and writes the addresses the request names
// in the caller's own page table. So a descriptor built here has to land on the guest heap the
// request was filled from, and a handler's ReadBuffer and WriteBuffer have to reach it.
TEST_CASE("TestKernel::ServiceHarness carries buffers to and from a handler", "[core][kernel]") {
    auto& harness = TestKernel::Harness();
    const auto service = harness.Open(std::make_shared<EchoService>(TestKernel::Shared()),
                                      TestKernel::SessionKind::Plain);

    const std::array<u64, 3> payload{0xfeed, 0, 0xbeef};
    const auto sent = harness.Process().Write(AsBytes(payload));
    const auto received = harness.Process().Reserve(sizeof(payload));
    REQUIRE(sent.address != received.address);

    TestKernel::RequestBuilder request{1};
    request.AddSendBuffer(sent.address, sent.size).AddReceiveBuffer(received.address,
                                                                    received.size);

    auto reply = harness.Call(service, request);
    REQUIRE(reply.GetResult() == ResultSuccess);
    REQUIRE(reply.Pop<u64>() == sizeof(payload));
    REQUIRE(harness.Process().Read(received.address, received.size) ==
            std::vector<u8>(AsBytes(payload).begin(), AsBytes(payload).end()));

    harness.Close(service);
}

// A descriptor's size is the only thing stopping a handler from writing past the buffer the guest
// gave it, so a receive buffer that is too small has to be respected rather than overrun.
TEST_CASE("TestKernel::ServiceHarness holds a handler to the receive buffer it was given",
          "[core][kernel]") {
    auto& harness = TestKernel::Harness();
    const auto service = harness.Open(std::make_shared<EchoService>(TestKernel::Shared()),
                                      TestKernel::SessionKind::Plain);

    const std::array<u64, 3> payload{0x1111, 0x2222, 0x3333};
    const auto sent = harness.Process().Write(AsBytes(payload));
    const auto received = harness.Process().Reserve(sizeof(u64));
    const auto beyond = harness.Process().Reserve(sizeof(payload));

    TestKernel::RequestBuilder request{1};
    request.AddSendBuffer(sent.address, sent.size).AddReceiveBuffer(received.address,
                                                                    received.size);

    auto reply = harness.Call(service, request);
    REQUIRE(reply.GetResult() == ResultSuccess);
    REQUIRE(reply.Pop<u64>() == sizeof(u64));
    REQUIRE(harness.Process().Read(received.address, sizeof(u64)) ==
            std::vector<u8>(AsBytes(payload).begin(), AsBytes(payload).begin() + sizeof(u64)));

    // Everything the rest of the payload would have run into, up to and past the next
    // reservation, which Reserve left zeroed.
    const u64 overrun = beyond.address + beyond.size - received.address - sizeof(u64);
    REQUIRE(harness.Process().Read(received.address + sizeof(u64), overrun) ==
            std::vector<u8>(overrun, 0));

    harness.Close(service);
}

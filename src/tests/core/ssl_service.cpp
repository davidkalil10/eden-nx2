// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "core/hle/result.h"
#include "core/hle/service/ssl/ssl.h"
#include "tests/core/ipc_request.h"
#include "tests/core/service_fixture.h"

namespace {

/// On ssl:s.
constexpr u32 CreateContext = 0;
constexpr u32 CreateContextForSystem = 100;

/// On the ISslContext either of them hands back.
constexpr u32 GetConnectionCount = 3;

/// Both names Service::SSL::LoopProcess registers, because the harness closes every port it
/// brought up and not only the one these tests open.
constexpr std::array SslServices{std::string_view{"ssl"}, std::string_view{"ssl:s"}};

/// What a guest writes for CreateContext and CreateContextForSystem alike. The service reads it
/// back as nn::ssl::sf::SslVersion plus padding plus a pid placeholder, a struct that lives inside
/// ssl.cpp; this is the same 0x10 bytes seen from the caller's side.
struct ContextParameters {
    u32 ssl_version;
    u32 padding;
    u64 pid_placeholder;
};
static_assert(sizeof(ContextParameters) == 0x10, "ContextParameters is an invalid size");

/// SslVersion::api_version is BitField<24, 7, u32>, so this is version 4 with no TLS bits set.
constexpr u32 ApiVersion4 = 4u << 24;

/// ssl:s brought up by its own LoopProcess and opened by name, which is the only way to reach it:
/// ISslServiceForSystem is declared inside ssl.cpp and no header names it.
struct SslForSystem {
    explicit SslForSystem(TestKernel::ServiceHarness& harness) {
        harness.StartModule(&Service::SSL::LoopProcess, SslServices);
        root = harness.OpenNamed("ssl:s", TestKernel::SessionKind::Domain);
    }

    TestKernel::SessionId root{};
};

} // Anonymous namespace

// CreateContextForSystem hands back a context, not a bare success.
TEST_CASE("Service::SSL ssl:s hands out a context for CreateContextForSystem", "[core][kernel]") {
    auto& harness = TestKernel::Harness();
    SslForSystem service{harness};

    TestKernel::RequestBuilder request{CreateContextForSystem};
    request.PushRaw(ContextParameters{ApiVersion4, 0, 0});

    auto created = harness.Call(service.root, request);
    REQUIRE(created.GetResult() == ResultSuccess);

    REQUIRE(created.DomainObjectCount() == 1);

    // Object 1 is ssl:s itself, so the context it just made is object 2.
    REQUIRE(created.DomainObject(0) == 2);

    harness.Close(service.root);
}

// Command 0, for a caller that names no process, hands back a context too.
TEST_CASE("Service::SSL ssl:s hands out a context for CreateContext", "[core][kernel]") {
    auto& harness = TestKernel::Harness();
    SslForSystem service{harness};

    TestKernel::RequestBuilder request{CreateContext};
    request.PushRaw(ContextParameters{ApiVersion4, 0, 0});

    auto created = harness.Call(service.root, request);
    REQUIRE(created.GetResult() == ResultSuccess);
    REQUIRE(created.DomainObjectCount() == 1);
    REQUIRE(created.DomainObject(0) == 2);

    harness.Close(service.root);
}

// An object id in a reply is only worth as much as what answers on it. A reply that carried an id
// nothing was registered under would pass the two tests above and still leave the caller with
// nothing, so the context has to answer a command only an ISslContext has: GetConnectionCount
// reports the connections made through this context, and a fresh one has none.
TEST_CASE("Service::SSL the context ssl:s hands out answers its own commands", "[core][kernel]") {
    auto& harness = TestKernel::Harness();
    SslForSystem service{harness};

    TestKernel::RequestBuilder request{CreateContext};
    request.PushRaw(ContextParameters{ApiVersion4, 0, 0});

    auto created = harness.Call(service.root, request);
    REQUIRE(created.GetResult() == ResultSuccess);
    REQUIRE(created.DomainObjectCount() == 1);

    const auto context = harness.OpenInterface(service.root, created);

    auto counted = harness.Call(context, TestKernel::RequestBuilder{GetConnectionCount});
    REQUIRE(counted.GetResult() == ResultSuccess);
    REQUIRE(counted.Pop<u32>() == 0);

    harness.Close(context);
    harness.Close(service.root);
}

// Each call builds a context of its own, and both commands build the same kind. Handing back one
// shared object instead would look right in a single-caller test and let one guest's connection
// count and options follow another's, which is what the per-context shared data exists to avoid.
TEST_CASE("Service::SSL ssl:s builds a context per call", "[core][kernel]") {
    auto& harness = TestKernel::Harness();
    SslForSystem service{harness};

    // Different api_versions, because a caller is free to ask for either and nothing about the
    // reply may depend on it. No command reads the version back out, so this is as far as a test
    // can follow it.
    TestKernel::RequestBuilder first{CreateContext};
    first.PushRaw(ContextParameters{ApiVersion4, 0, 0});
    TestKernel::RequestBuilder second{CreateContextForSystem};
    second.PushRaw(ContextParameters{0, 0, 0x4142'4344'4546'4748});

    auto created = harness.Call(service.root, first);
    auto created_for_system = harness.Call(service.root, second);

    REQUIRE(created.GetResult() == ResultSuccess);
    REQUIRE(created_for_system.GetResult() == ResultSuccess);
    REQUIRE(created.DomainObjectCount() == 1);
    REQUIRE(created_for_system.DomainObjectCount() == 1);
    REQUIRE(created.DomainObject(0) != created_for_system.DomainObject(0));

    const auto context = harness.OpenInterface(service.root, created);
    const auto context_for_system = harness.OpenInterface(service.root, created_for_system);

    auto counted = harness.Call(context, TestKernel::RequestBuilder{GetConnectionCount});
    REQUIRE(counted.GetResult() == ResultSuccess);
    REQUIRE(counted.Pop<u32>() == 0);

    auto counted_for_system =
        harness.Call(context_for_system, TestKernel::RequestBuilder{GetConnectionCount});
    REQUIRE(counted_for_system.GetResult() == ResultSuccess);
    REQUIRE(counted_for_system.Pop<u32>() == 0);

    harness.Close(context_for_system);
    harness.Close(context);
    harness.Close(service.root);
}

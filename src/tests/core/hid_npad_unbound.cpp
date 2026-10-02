// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <mutex>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "core/core.h"
#include "core/hle/service/kernel_helpers.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad.h"
#include "tests/core/kernel_fixture.h"

// HID marks an applet's resource assigned before the npad binds its controllers, and a
// controller change can arrive in between (Android's virtual pad sends one every frame).
TEST_CASE("Service::HID a controller change passes over an applet whose controllers are not "
          "bound yet",
          "[core][kernel][hid]") {
    auto& system = TestKernel::Shared();
    Service::KernelHelpers::ServiceContext context{system, "hid_npad_unbound"};
    auto resource = std::make_shared<Service::HID::AppletResource>(system);
    std::recursive_mutex shared_mutex;
    std::mutex input_mutex;
    auto* const input_event = context.CreateEvent("hid_npad_unbound:input");
    auto handheld = std::make_shared<Service::HID::HandheldConfig>();

    constexpr u64 Bound = 0x51;
    constexpr u64 Unbound = 0x55;
    {
        Service::HID::NPad npad{system.HIDCore(), context};
        npad.SetNpadExternals(resource, &shared_mutex, handheld, input_event, &input_mutex,
                              nullptr);

        // One applet as HID leaves it once it is up: assigned, and its controllers bound.
        REQUIRE(resource->RegisterAppletResourceUserId(Bound, true).IsSuccess());
        REQUIRE(resource->CreateAppletResource(Bound).IsSuccess());
        REQUIRE(npad.RegisterAppletResourceUserId(Bound).IsSuccess());
        REQUIRE(npad.Activate().IsSuccess());
        REQUIRE(npad.Activate(Bound).IsSuccess());

        // A second one caught in between: assigned, nothing bound yet.
        REQUIRE(resource->RegisterAppletResourceUserId(Unbound, true).IsSuccess());
        REQUIRE(resource->CreateAppletResource(Unbound).IsSuccess());

        // A pad of a style the session accepts, so connecting it is a change the npad hears.
        auto& hid_core = system.HIDCore();
        auto* const player = hid_core.GetEmulatedControllerByIndex(0);
        const auto saved_tag = hid_core.GetSupportedStyleTag();
        const auto saved_style = player->GetNpadStyleIndex();
        const bool was_connected = player->IsConnected();
        hid_core.SetSupportedStyleTag({.raw = Core::HID::NpadStyleSet::All});
        player->Disconnect();
        player->SetNpadStyleIndex(Core::HID::NpadStyleIndex::Fullkey);

        player->Connect();
        REQUIRE(player->IsConnected());
        player->Disconnect();
        REQUIRE_FALSE(player->IsConnected());

        player->SetNpadStyleIndex(saved_style);
        hid_core.SetSupportedStyleTag(saved_tag);
        if (was_connected) {
            player->Connect();
        }
    }

    resource->FreeAppletResourceId(Unbound);
    resource->FreeAppletResourceId(Bound);
    context.CloseEvent(input_event);
}

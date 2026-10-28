// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/svc_types.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/os/process.h"
#include "core/loader/loader.h"

namespace {

using namespace std::chrono_literals;

constexpr int Boots = 3;

/// A bound to wait up to, not a sleep: three boots without a game take well under a second.
constexpr auto Deadline = 10s;

/// Loads nothing. The process is made the way RunOnHostCoreProcess makes one, which is all a
/// process handed to AM needs to be torn down the way a game's is.
class BareLoader final : public Loader::AppLoader {
public:
    BareLoader() : AppLoader{nullptr} {}

    Loader::FileType GetFileType() const override {
        return Loader::FileType::Unknown;
    }

    LoadResult Load(Kernel::KProcess& process, Core::System& system) override {
        auto& kernel = system.Kernel();
        if (R_FAILED(process.Initialize(kernel, Kernel::Svc::CreateProcessParameter{},
                                        kernel.GetSystemResourceLimit(), false))) {
            return {Loader::ResultStatus::ErrorNotInitialized, std::nullopt};
        }
        return {Loader::ResultStatus::Success, LoadParameters{44, 0x1000}};
    }
};

struct Outcome {
    int handed_over{};
    int left_behind{};
};

/// Each boot goes as far as Load handing the AppletManager its process, and stops before AM has
/// run to take it, as a Stop during boot does.
Outcome BootAndStop() {
    Core::System system;
    system.Initialize();

    Outcome outcome;
    for (int boot = 0; boot < Boots; ++boot) {
        system.Kernel().Initialize();

        BareLoader loader;
        Loader::ResultStatus status{};
        auto process = std::make_unique<Service::Process>(system);
        if (process->Initialize(loader, status)) {
            system.GetAppletManager().CreateAndInsertByFrontendAppletParameters(
                std::move(process), Service::AM::FrontendAppletParameters{});
        }
        if (system.GetAppletManager().HasPendingProcess()) {
            ++outcome.handed_over;
        }

        system.ShutdownMainProcess();
        if (system.GetAppletManager().HasPendingProcess()) {
            ++outcome.left_behind;
        }
    }
    return outcome;
}

} // Anonymous namespace

// A Stop before AM takes its process must not leave it in the AppletManager, where the next
// Load would destroy it against the previous session's kernel.
TEST_CASE("System::ShutdownMainProcess leaves no process in the AppletManager", "[core][hle]") {
    // A System and a thread of its own, for the reasons tests/core/service_stop.cpp gives.
    auto result = std::make_shared<std::promise<Outcome>>();
    auto outcome = result->get_future();
    std::thread worker{[result] { result->set_value(BootAndStop()); }};

    const bool finished = outcome.wait_for(Deadline) == std::future_status::ready;
    if (finished) {
        worker.join();
    } else {
        worker.detach();
    }
    REQUIRE(finished);

    const auto [handed_over, left_behind] = outcome.get();
    REQUIRE(handed_over == Boots);
    REQUIRE(left_behind == 0);
}

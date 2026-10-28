// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/vi/vi.h"

namespace {

using namespace std::chrono_literals;

/// Long next to the few milliseconds a teardown takes. A bound to wait up to, not a sleep.
constexpr auto Deadline = 5s;

/// Everything vi can still reach. Leaked when vi does not return, so it stays blocked on memory
/// that is still there instead of on a destroyed System.
struct Session {
    Core::System system;
    std::stop_source vi_stop;
    Common::Event returned;
};

/// Starts vi with no dispdrv to wait for, then stops and tears down as Stop does once vi waits.
/// True if vi got to the wait and its LoopProcess returned, each within the deadline.
bool StopWhileViWaits() {
    auto session = std::make_unique<Session>();
    auto& system = session->system;
    system.Initialize();
    system.Kernel().Initialize();
    system.InitializeServiceManager();

    // A host thread, as Service::Services gives vi, but with no process behind it: the thread
    // RunOnHostCoreProcess makes is closed after the module returns, which here is during the
    // teardown, racing the kernel shutdown at its end.
    std::thread vi{[&s = *session] {
        Service::VI::LoopProcess(s.system, s.vi_stop.get_token());
        s.returned.Set();
    }};
    const auto leave_vi_running = [&] {
        vi.detach();
        static_cast<void>(session.release());
        return false;
    };

    // Not before vi waits: until then it may still be reading the service manager the teardown
    // destroys. Once it waits, the manager's destructor waits for it to leave.
    const auto deadline = std::chrono::steady_clock::now() + Deadline;
    while (system.ServiceManager().HostWaiters() == 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return leave_vi_running();
        }
        std::this_thread::sleep_for(1ms);
    }
    system.ShutdownMainProcess();

    if (!session->returned.WaitUntil(deadline)) {
        return leave_vi_running();
    }
    vi.join();
    return true;
}

} // Anonymous namespace

// A Stop while the services start ends a wait for a service that has not registered (vi
// waits for dispdrv, audio for set:sys).
TEST_CASE("System::ShutdownMainProcess ends a service still waiting for another", "[core][hle]") {
    // A System of its own: the stop and the teardown are ShutdownMainProcess, which would take
    // the shared kernel down with it. On a thread of its own: the kernel binds a host thread to
    // the first kernel that registers it, and this one must be new.
    auto result = std::make_shared<std::promise<bool>>();
    auto outcome = result->get_future();
    std::thread worker{[result] { result->set_value(StopWhileViWaits()); }};

    // Also bounds ShutdownMainProcess itself, which is what hung.
    const bool finished = outcome.wait_for(2 * Deadline) == std::future_status::ready;
    if (finished) {
        worker.join();
    } else {
        worker.detach();
    }
    REQUIRE(finished);
    REQUIRE(outcome.get());
}

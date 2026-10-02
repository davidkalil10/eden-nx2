// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/hle/kernel/k_light_lock.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "tests/core/kernel_fixture.h"
#include "tests/core/service_fixture.h"

namespace {

using namespace std::chrono_literals;

/// A dummy KThread owned by @p process, reserved, initialized and registered.
Kernel::KThread* MakeOwnedDummyThread(Core::System& system, Kernel::KProcess& process) {
    auto& kernel = system.Kernel();
    Kernel::KScopedResourceReservation reservation{kernel, std::addressof(process),
                                                   Kernel::LimitableResource::ThreadCountMax};
    REQUIRE(reservation.Succeeded());
    auto* thread = Kernel::KThread::Create(kernel);
    REQUIRE(R_SUCCEEDED(
        Kernel::KThread::InitializeDummyThread(system, thread, std::addressof(process))));
    reservation.Commit();
    Kernel::KThread::Register(kernel, thread);
    return thread;
}

/// A host thread running as such a dummy thread, as the kernel's own RunHostThreadFunc makes one.
class OwnedHostThread {
public:
    OwnedHostThread(Core::System& system, Kernel::KProcess& process, std::function<void()> body)
        : kthread{MakeOwnedDummyThread(system, process)}, finished{done.get_future()} {
        host = std::thread{[&kernel = system.Kernel(), this, body = std::move(body)] {
            kernel.RegisterHostThread(kthread);
            body();
            kthread->Close(kernel);
            done.set_value();
        }};
    }

    ~OwnedHostThread() {
        // A thread still blocked here belongs to a test that already failed; joining it would
        // hang the suite.
        if (finished.wait_for(0ms) == std::future_status::ready) {
            host.join();
        } else {
            host.detach();
        }
    }

    [[nodiscard]] Kernel::KThread& Thread() const {
        return *kthread;
    }

    [[nodiscard]] bool Finished(std::chrono::milliseconds within) const {
        return finished.wait_for(within) == std::future_status::ready;
    }

private:
    Kernel::KThread* kthread;
    std::promise<void> done;
    std::future<void> finished;
    std::thread host;
};

template <typename Predicate>
bool Within(std::chrono::milliseconds limit, Predicate&& predicate) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

} // Anonymous namespace

// A finished thread's last Close takes its process's list lock, which cannot be waited for under
// the scheduler lock. AM's event observer holds that list lock in SetActivity while an applet
// exits.
TEST_CASE("Kernel::KThread a finished thread's last Close waits for its process's list lock",
          "[core][kernel]") {
    auto& system = TestKernel::Shared();
    auto& kernel = system.Kernel();
    auto& process = TestKernel::Harness().Process().Process();
    auto& list_lock = process.GetListLock();

    auto* const victim = MakeOwnedDummyThread(system, process);
    const auto registered = [&process, victim] {
        for (const auto& thread : process.GetThreadList()) {
            if (std::addressof(thread) == victim) {
                return true;
            }
        }
        return false;
    };
    REQUIRE(registered());

    // Both host threads exist before the lock is taken: making one registers it with the process,
    // under the same list lock.
    std::atomic<bool> held{false};
    std::atomic<bool> release{false};
    OwnedHostThread worker{system, process, [&] {
                               while (!held) {
                                   std::this_thread::sleep_for(1ms);
                               }
                               victim->DoWorkerTaskImpl(kernel);
                           }};
    OwnedHostThread holder{system, process, [&] {
                               list_lock.Lock();
                               held = true;
                               while (!release) {
                                   std::this_thread::sleep_for(1ms);
                               }
                               list_lock.Unlock();
                           }};
    REQUIRE(Within(5000ms, [&] { return held.load(); }));
    REQUIRE(Within(5000ms, [&] {
        return worker.Thread().GetState() == Kernel::ThreadState::Waiting;
    }));

    // Room for a worker that did not really wait to get past the lock.
    std::this_thread::sleep_for(100ms);
    CHECK(list_lock.IsLocked());
    CHECK(registered());
    CHECK_FALSE(worker.Finished(0ms));

    release = true;
    REQUIRE(holder.Finished(5000ms));
    REQUIRE(worker.Finished(5000ms));
    CHECK_FALSE(list_lock.IsLocked());
    CHECK_FALSE(registered());
}

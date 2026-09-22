// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "core/core.h"
#include "core/core_timing.h"

namespace {
// Numbers are chosen randomly to make sure the correct one is given.
constexpr std::array<u64, 5> calls_order{{2, 0, 1, 4, 3}};
std::array<s64, 5> delays{};
std::bitset<5> callbacks_ran_flags;
u64 expected_callback = 0;

template <unsigned int IDX>
std::optional<std::chrono::nanoseconds> HostCallbackTemplate(s64 time,
                                                             std::chrono::nanoseconds ns_late) {
    static_assert(IDX < callbacks_ran_flags.size(), "IDX out of range");
    callbacks_ran_flags.set(IDX);
    delays[IDX] = ns_late.count();
    ++expected_callback;
    return std::nullopt;
}

struct ScopeInit final {
    ScopeInit() {
        core_timing.SetMulticore(true);
        core_timing.Initialize([]() {});
    }

    Core::Timing::CoreTiming core_timing;
};

u64 TestTimerSpeed(Core::Timing::CoreTiming& core_timing) {
    const u64 start = core_timing.GetGlobalTimeNs().count();
    volatile u64 placebo = 0;
    for (std::size_t i = 0; i < 1000; i++) {
        placebo = placebo + core_timing.GetGlobalTimeNs().count();
    }
    const u64 end = core_timing.GetGlobalTimeNs().count();
    return end - start;
}

} // Anonymous namespace

TEST_CASE("CoreTiming[BasicOrder]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;
    std::vector<std::shared_ptr<Core::Timing::EventType>> events{
        Core::Timing::CreateEvent("callbackA", HostCallbackTemplate<0>),
        Core::Timing::CreateEvent("callbackB", HostCallbackTemplate<1>),
        Core::Timing::CreateEvent("callbackC", HostCallbackTemplate<2>),
        Core::Timing::CreateEvent("callbackD", HostCallbackTemplate<3>),
        Core::Timing::CreateEvent("callbackE", HostCallbackTemplate<4>),
    };

    expected_callback = 0;

    core_timing.SyncPause(true);

    const u64 one_micro = 1000U;
    for (std::size_t i = 0; i < events.size(); i++) {
        const u64 order = calls_order[i];
        const auto future_ns = std::chrono::nanoseconds{static_cast<s64>(i * one_micro + 100)};

        core_timing.ScheduleEvent(future_ns, events[order]);
    }
    /// test pause
    REQUIRE(callbacks_ran_flags.none());

    core_timing.Pause(false); // No need to sync

    while (core_timing.HasPendingEvents())
        ;

    REQUIRE(callbacks_ran_flags.all());

    for (std::size_t i = 0; i < delays.size(); i++) {
        const double delay = static_cast<double>(delays[i]);
        const double micro = delay / 1000.0f;
        const double mili = micro / 1000.0f;
        printf("HostTimer Pausing Delay[%zu]: %.3f %.6f\n", i, micro, mili);
    }
}

TEST_CASE("CoreTiming[BasicOrderNoPausing]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;
    std::vector<std::shared_ptr<Core::Timing::EventType>> events{
        Core::Timing::CreateEvent("callbackA", HostCallbackTemplate<0>),
        Core::Timing::CreateEvent("callbackB", HostCallbackTemplate<1>),
        Core::Timing::CreateEvent("callbackC", HostCallbackTemplate<2>),
        Core::Timing::CreateEvent("callbackD", HostCallbackTemplate<3>),
        Core::Timing::CreateEvent("callbackE", HostCallbackTemplate<4>),
    };

    core_timing.SyncPause(true);
    core_timing.SyncPause(false);

    expected_callback = 0;

    const u64 start = core_timing.GetGlobalTimeNs().count();
    const u64 one_micro = 1000U;

    for (std::size_t i = 0; i < events.size(); i++) {
        const u64 order = calls_order[i];
        const auto future_ns = std::chrono::nanoseconds{static_cast<s64>(i * one_micro + 100)};
        core_timing.ScheduleEvent(future_ns, events[order]);
    }

    const u64 end = core_timing.GetGlobalTimeNs().count();
    const double scheduling_time = static_cast<double>(end - start);
    const double timer_time = static_cast<double>(TestTimerSpeed(core_timing));

    while (core_timing.HasPendingEvents())
        ;

    REQUIRE(callbacks_ran_flags.all());

    for (std::size_t i = 0; i < delays.size(); i++) {
        const double delay = static_cast<double>(delays[i]);
        const double micro = delay / 1000.0f;
        const double mili = micro / 1000.0f;
        printf("HostTimer No Pausing Delay[%zu]: %.3f %.6f\n", i, micro, mili);
    }

    const double micro = scheduling_time / 1000.0f;
    const double mili = micro / 1000.0f;
    printf("HostTimer No Pausing Scheduling Time: %.3f %.6f\n", micro, mili);
    printf("HostTimer No Pausing Timer Time: %.3f %.6f\n", timer_time / 1000.f,
           timer_time / 1000000.f);
}

TEST_CASE("CoreTiming[ResetKeepsTheStop]", "[core]") {
    // The timer thread reads the stop token only after a wait returns, so Reset has to request
    // the stop before it signals. Waiting for paused_set hits that window on purpose.
    constexpr int attempts = 64;
    constexpr int reinitializations = 8;
    auto const finished = std::make_shared<std::atomic<int>>(0);

    // Detached, with a shared counter: a worker that never comes back fails the count instead
    // of hanging the suite on a join.
    std::thread([finished] {
        // Bounded, so a build that never starts a timer thread fails the count instead of
        // spinning here forever.
        auto const wait_for = [](auto&& ready) {
            auto const by = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!ready() && std::chrono::steady_clock::now() < by) {
            }
        };

        for (int i = 0; i < attempts; ++i) {
            {
                Core::Timing::CoreTiming core_timing;
                core_timing.SetMulticore(true);
                core_timing.Initialize([] {});
                // A fresh object publishes paused_set only once the thread is on its way into
                // pause_event.Wait(), which is where every shutdown finds it.
                wait_for([&core_timing] { return !core_timing.IsRunning(); });
            }
            finished->fetch_add(1, std::memory_order_release);
        }

        // Initialize resets first, so a second game on one System goes through that same stop
        // request, and only a timer thread that came back sets has_started again.
        Core::Timing::CoreTiming reused;
        reused.SetMulticore(true);
        for (int i = 0; i < reinitializations; ++i) {
            reused.Initialize([] {});
            wait_for([&reused] { return reused.HasStarted(); });
            if (!reused.HasStarted()) {
                return;
            }
            finished->fetch_add(1, std::memory_order_release);
        }
    }).detach();

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (finished->load(std::memory_order_acquire) < attempts + reinitializations &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(finished->load(std::memory_order_acquire) == attempts + reinitializations);
}

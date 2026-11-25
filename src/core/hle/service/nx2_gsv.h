// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <thread>
#include <chrono>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <format>
#include <initializer_list>
#include <span>
#include <atomic>
#include <map>
#include <optional>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

#include "common/alignment.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/kernel/svc_results.h"
#include "video_core/renderer_base.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/perf_stats.h"
#include "video_core/gpu.h"
#include "video_core/framebuffer_config.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/k_system_resource.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_page_group.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/k_server_session.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/memory.h"
#include "video_core/host1x/host1x.h"
#include "video_core/nx2.h"
#include "core/hle/service/hle_ipc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nvdrv/devices/nx2_stub_device.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nx2_stub_interface.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service {

// Creates a session served by `iface` and returns its client end. Unlike PushIpcInterface this
// works from a domain too, for callers that want a raw session handle.
template <class T>
inline Kernel::KClientSession* CreateRawSession(Core::System& system, HLERequestContext& ctx,
                                                std::shared_ptr<T> iface) {
    auto& kernel = system.Kernel();
    ASSERT(Kernel::GetCurrentProcess(kernel).GetResourceLimit()->Reserve(
        kernel, Kernel::LimitableResource::SessionCountMax, 1));

    auto* session = Kernel::KSession::Create(kernel);
    session->Initialize(kernel, nullptr, 0);
    Kernel::KSession::Register(kernel, session);

    auto manager =
        std::make_shared<SessionRequestManager>(kernel, ctx.GetManager()->GetServerManager());
    manager->SetSessionHandler(std::move(iface));
    ctx.GetManager()->GetServerManager().RegisterSession(&session->GetServerSession(), manager);
    return &session->GetClientSession();
}

// Replies to a TIPC request with success and raw u32 data words. ResponseBuilder::Push(Result)
// writes the CMIF two-word result, which shifts TIPC data one word past where the client reads
// it (and past the copied size); TIPC has a one-word result followed directly by the data.
inline void TipcReplyWords(HLERequestContext& ctx, std::span<const u32> words) {
    IPC::ResponseBuilder rb{ctx, static_cast<u32>(2 + words.size())};
    rb.PushRaw<u32>(ResultSuccess.raw);
    for (const u32 w : words) {
        rb.Push<u32>(w);
    }
}

inline void TipcReplyWords(HLERequestContext& ctx, std::initializer_list<u32> words) {
    TipcReplyWords(ctx, std::span<const u32>{words.begin(), words.size()});
}

inline void TipcHandshake(HLERequestContext& ctx, std::string_view session) {
    const auto* w = ctx.CommandBuffer();
    LOG_INFO(Service, "{} TIPC 0 handshake {:#x}", session, u64{w[2]} | (u64{w[3]} << 32));
    TipcReplyWords(ctx, {w[2], w[3]});
}

// Replies with success and a copied handle to a new session served by `iface`.
template <class T>
inline void ReturnRawSession(Core::System& system, HLERequestContext& ctx, std::shared_ptr<T> iface) {
    auto* client = CreateRawSession(system, ctx, std::move(iface));
    IPC::ResponseBuilder rb{ctx, 2, 1, 0};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, *client);
}

// Placeholder for the Switch 2 NVIDIA driver server (nn::nvsrv). It has no handlers, so every
// command (CMIF or TIPC) gets an empty success and is logged through the nx2: prefix.
class INx2NvsrvSession final : public ServiceFramework<INx2NvsrvSession> {
public:
    explicit INx2NvsrvSession(Core::System& system_) : ServiceFramework{system_, "nx2:nvsrv"} {}
};

// Placeholder for the Switch 2 syncpoint server (nn::syncpt), handed out by gsv 202. TIPC 1
// returns a u32 the SDK requires to be < 0x400 and then passes min(value, 63) to TIPC 21.
// Everything else is auto-stubbed through the nx2: prefix.
// - TIPC 0(u64): handshake, echo the token (newer SDKs).
// - TIPC 22(u32 slot): an event handle for that slot (copy handle at word 3, result at word 4).
//   NVN's NvOsEventPool asks for slots and multi-waits on them.
// - TIPC 201(slot, counter, u64 value, mode): signal the slot's event once the counter reaches
//   value. The counter is a SyncCounterId, the bitwise NOT of the host1x syncpoint id; mode 0
//   compares 32-bit wrapping, mode 1 64-bit. Eden's guest syncpoints back it.
// - TIPC 202(slot): cancel the slot's request.
class INx2SyncptSession final : public ServiceFramework<INx2SyncptSession> {
public:
    explicit INx2SyncptSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:syncpt"}, m_context{system_, "nx2:syncpt"} {
        RegisterHandlersTipc({
            {0, &INx2SyncptSession::Handshake, "Handshake"},
            {1, &INx2SyncptSession::GetCount, "GetCount (guessed)"},
            {22, &INx2SyncptSession::GetSlotEvent, "GetSlotEvent (guessed)"},
            {103, &INx2SyncptSession::QueryCounter, "QueryCounter"},
            {201, &INx2SyncptSession::RequestSlotEvent, "RequestSlotEvent (guessed)"},
            {202, &INx2SyncptSession::CancelSlotEvent, "CancelSlotEvent (guessed)"},
        });
    }

    ~INx2SyncptSession() override {
        {
            std::scoped_lock lk{m_shared->mutex};
            m_shared->alive = false;
        }
        for (auto& [slot, request] : m_requests) {
            Cancel(request);
        }
        for (auto& [slot, event] : m_events) {
            m_context.CloseEvent(event);
        }
    }

private:
    // Newer SDKs (title 1) open with TIPC 0(u64 token) and abort unless the reply echoes it.
    void Handshake(HLERequestContext& ctx) {
        TipcHandshake(ctx, "nx2:syncpt");
    }

    void GetCount(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:syncpt TIPC 1, returning 32");
        TipcReplyWords(ctx, {32});
    }

    // From changes.patch: the counter is the bitwise NOT of the host1x syncpoint id.
    void QueryCounter(HLERequestContext& ctx) {
        const u32 syncpoint = ~ctx.CommandBuffer()[2];
        const u64 value = syncpoint < HostSyncpoints
                              ? system.Host1x().GetSyncpointManager().GetGuestSyncpointValue(syncpoint)
                              : 0;
        LOG_DEBUG(Service, "nx2:syncpt 103 syncpoint={} value={:#x}", syncpoint, value);
        TipcReplyWords(ctx, {static_cast<u32>(value), static_cast<u32>(value >> 32), 0});
    }

    void GetSlotEvent(HLERequestContext& ctx) {
        const u32 slot = ctx.CommandBuffer()[2];
        LOG_WARNING(Service, "(STUBBED) nx2:syncpt TIPC 22 slot={:#x}, returning an event", slot);
        auto*& event = m_events[slot];
        if (event == nullptr) {
            event = m_context.CreateEvent(std::format("nx2:syncpt:Slot{:x}", slot));
        }
        IPC::ResponseBuilder rb{ctx, 2, 1, 0};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, event->GetReadableEvent());
    }

    // Map nodes are stable, so a registered action can hold a reference to its request.
    struct SlotRequest {
        u32 syncpoint{};
        std::atomic<bool> pending{};
        Tegra::Host1x::SyncpointManager::ActionHandle handle{};
    };

    struct SharedState {
        std::mutex mutex;
        bool alive{true};
    };

    void RequestSlotEvent(HLERequestContext& ctx) {
        const auto* cmd_buf = ctx.CommandBuffer();
        const u32 slot = cmd_buf[2];
        const u32 syncpoint = ~cmd_buf[3];
        const u64 value = u64{cmd_buf[4]} | (u64{cmd_buf[5]} << 32);
        const u32 mode = cmd_buf[6];
        LOG_DEBUG(Service, "nx2:syncpt TIPC 201 slot={:#x} syncpoint={} value={:#x} mode={}", slot,
                  syncpoint, value, mode);
        auto*& event = m_events[slot];
        if (event == nullptr) {
            event = m_context.CreateEvent(std::format("nx2:syncpt:Slot{:x}", slot));
        }
        if (syncpoint >= HostSyncpoints) {
            LOG_WARNING(Service, "nx2:syncpt TIPC 201 syncpoint {} out of range, signalling",
                        syncpoint);
            event->Signal(system.Kernel());
            TipcReplyWords(ctx, {});
            return;
        }
        auto& request = m_requests[slot];
        Cancel(request);
        request.syncpoint = syncpoint;
        request.pending = true;
        auto& manager = system.Host1x().GetSyncpointManager();
        // The action runs on the GPU thread under the manager's lock, or inline if the value is
        // already reached.
        request.handle = manager.RegisterGuestAction(
            syncpoint, static_cast<u32>(value),
            [shared = m_shared, &kernel = system.Kernel(), &request, event] {
                std::scoped_lock lk{shared->mutex};
                if (!shared->alive) {
                    return;
                }
                request.pending = false;
                event->Signal(kernel);
            });
        TipcReplyWords(ctx, {});
    }

    void CancelSlotEvent(HLERequestContext& ctx) {
        const u32 slot = ctx.CommandBuffer()[2];
        LOG_DEBUG(Service, "nx2:syncpt TIPC 202 slot={:#x}", slot);
        if (auto it = m_requests.find(slot); it != m_requests.end() && Cancel(it->second)) {
            if (const auto event = m_events.find(slot); event != m_events.end()) {
                event->second->Signal(system.Kernel());
            }
        }
        TipcReplyWords(ctx, {});
    }

    bool Cancel(SlotRequest& request) {
        if (request.pending.exchange(false)) {
            system.Host1x().GetSyncpointManager().DeregisterGuestAction(request.syncpoint,
                                                                        request.handle);
            return true;
        }
        return false;
    }

    static constexpr u32 HostSyncpoints = 192; // Tegra::Host1x::SyncpointManager

    KernelHelpers::ServiceContext m_context;
    std::map<u32, Kernel::KEvent*> m_events;
    std::map<u32, SlotRequest> m_requests;
    std::shared_ptr<SharedState> m_shared = std::make_shared<SharedState>();
};

// Placeholder for the Switch 2 GPU session (nn::gpu::sdk), handed out by gsv 497.
class INx2GpuSession final : public ServiceFramework<INx2GpuSession> {
public:
    explicit INx2GpuSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:gpu"}, m_context{system_, "nx2:gpu"} {
        RegisterHandlersTipc({
            {0, &INx2GpuSession::Handshake, "Handshake"},
            {11, &INx2GpuSession::GetIdRange, "GetIdRange (guessed)"},
            {12, &INx2GpuSession::GetIdEvent, "GetIdEvent (guessed)"},
            {21, &INx2GpuSession::Start, "Start (guessed)"},
            {22, &INx2GpuSession::OpenSubSession, "OpenSubSession (guessed)"},
            {33, &INx2GpuSession::GetCount33, "Unknown33 (u32 count)"},
            {41, &INx2GpuSession::CreateGraphicsDevice, "CreateGraphicsDevice"},
            {42, &INx2GpuSession::DestroyGraphicsDevice, "DestroyGraphicsDevice"},
            {201, &INx2GpuSession::GetUserPartition, "GetUserPartition (newer SDKs)"},
            {202, &INx2GpuSession::GetGsvPartition, "GetGsvPartition (newer SDKs)"},
            {203, &INx2GpuSession::GetVaRange, "GetVaRange (guessed)"},
            // Sub-session commands (gpu 22 hands out a second session to this same object).
            {101, &INx2GpuSession::SubRegisterMemory, "CreateGpuAccessibleMemory"},
            {102, &INx2GpuSession::SubDestroyMemory, "DestroyGpuMemory"},
            {211, &INx2GpuSession::SubSubmitJob, "NormalMapGpuMemory4K"},
            {212, &INx2GpuSession::SubModifyKind, "NormalModifyMemoryKind4K"},
            {213, &INx2GpuSession::SubUnmapJob, "NormalUnmapGpuMemory4K"},
            {214, &INx2GpuSession::SubCreateGpuOnlyRegion, "CreateGpuOnlyRegion (guessed)"},
            {23, &INx2GpuSession::StubReplyZeroed, "Unknown23"},
            {24, &INx2GpuSession::StubReplyZeroed, "Unknown24"},
            {25, &INx2GpuSession::StubReplyZeroed, "Unknown25"},
            {34, &INx2GpuSession::StubReplyZeroed, "Unknown34"},
            {51, &INx2GpuSession::StubReplyZeroed, "Unknown51"},
            {8001, &INx2GpuSession::StubReplyZeroed, "Unknown8001"},
            {8002, &INx2GpuSession::StubReplyZeroed, "Unknown8002"},
            {8003, &INx2GpuSession::StubReplyZeroed, "Unknown8003"},
            {8004, &INx2GpuSession::StubReplyZeroed, "Unknown8004"},
        });
    }

    ~INx2GpuSession() override {
        for (auto* event : m_events) {
            if (event != nullptr) {
                m_context.CloseEvent(event);
            }
        }
    }

private:
    void Handshake(HLERequestContext& ctx) {
        TipcHandshake(ctx, "nx2:gpu");
    }

    void StubReplyZeroed(HLERequestContext& ctx) {
        static constexpr std::array<u32, 32> zeros{};
        TipcReplyWords(ctx, zeros);
    }

    static constexpr u32 NumIds = 32;

    Kernel::KEvent* EventFor(u32 id) {
        id %= NumIds;
        if (m_events[id] == nullptr) {
            m_events[id] = m_context.CreateEvent(std::format("nx2:gpu:Event{}", id));
        }
        return m_events[id];
    }

    void GetIdRange(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gpu TIPC 11, returning [0, {}]", NumIds - 1);
        TipcReplyWords(ctx, {0, NumIds - 1});
    }

    void GetIdEvent(HLERequestContext& ctx) {
        const u32 id = ctx.CommandBuffer()[2];
        LOG_WARNING(Service, "(STUBBED) nx2:gpu TIPC 12 id={}", id);
        IPC::ResponseBuilder rb{ctx, 2, 1, 0};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, EventFor(id)->GetReadableEvent());
    }

    void Start(HLERequestContext& ctx) {
        const u32 id = ctx.CommandBuffer()[2];
        LOG_WARNING(Service, "(STUBBED) nx2:gpu TIPC 21 id={}, signalling completion", id);
        EventFor(id)->Signal(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetCount33(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gpu TIPC 33 [2]={:#x}, returning 1", ctx.CommandBuffer()[2]);
        TipcReplyWords(ctx, {1});
    }

    // Used by nn::gpu::sdk::CreateGpuAddressSpace: three u64s after the result, a kind (< 5, 0 =
    // none) and the GPU VA range of the in-process VirtualMachine that serves AllocSpace
    // (nn::gpu::sdk::util::ExecuteVirtualMachineAllocSpace). NVN reserves fixed VA from 0x8000000
    // up to 512 GiB, so report the same range NvRm gives AllocAsEx: [0x8000000, 0xC000000000).
    void GetVaRange(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gpu TIPC 203 [2]={:#x}", ctx.CommandBuffer()[2]);
        constexpr u64 kind = 1;
        constexpr u64 start = 0x8000000ULL;
        constexpr u64 size = 0xC000000000ULL - start;
        TipcReplyWords(ctx, {static_cast<u32>(kind), static_cast<u32>(kind >> 32),
                             static_cast<u32>(start), static_cast<u32>(start >> 32),
                             static_cast<u32>(size), static_cast<u32>(size >> 32)});
    }

    // Newer SDKs (title 1) query the address space's user and gsv partitions with 201/202(id):
    // two u64s each, stored as the partition's min/max. The user value works whether it is read
    // as (start, size) or (min, max). Title 4 never needed the gsv partition, so it stays empty.
    void GetUserPartition(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gpu TIPC 201 [2]={:#x}", ctx.CommandBuffer()[2]);
        constexpr u64 first = 0x8000000ULL;
        constexpr u64 second = 0xC000000000ULL - first;
        TipcReplyWords(ctx, {static_cast<u32>(first), static_cast<u32>(first >> 32),
                             static_cast<u32>(second), static_cast<u32>(second >> 32)});
    }

    void GetGsvPartition(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gpu TIPC 202 [2]={:#x}, empty", ctx.CommandBuffer()[2]);
        // Empty partition is min ~0, max 0 (not 0, 0 which the SDK reads as a 1-byte partition).
        TipcReplyWords(ctx, {0xffffffffU, 0xffffffffU, 0, 0});
    }

    void CreateGraphicsDevice(HLERequestContext& ctx) {
        const auto* b = ctx.CommandBuffer();
        const u64 name = u64{b[2]} | (u64{b[3]} << 32);
        const u64 flags = u64{b[4]} | (u64{b[5]} << 32);
        u32 id = 0;
        for (u32 candidate = 1; candidate < m_devices.size(); ++candidate) {
            if (!m_devices[candidate]) {
                m_devices[candidate] = true;
                id = candidate;
                break;
            }
        }
        LOG_INFO(Service, "nx2:gpu 41 create graphics device name={:#x} flags={:#x} -> id {}", name,
                 flags, id);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(id != 0 ? ResultSuccess : Kernel::ResultOutOfResource);
        rb.Push(id);
    }

    void DestroyGraphicsDevice(HLERequestContext& ctx) {
        const u32 id = ctx.CommandBuffer()[2] & 0xff;
        const bool existed = id < m_devices.size() && std::exchange(m_devices[id], false);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(existed ? ResultSuccess : Kernel::ResultNotFound);
    }

    void OpenSubSession(HLERequestContext& ctx) {
        // Served by this object so sub-session jobs can signal the per-id events.
        LOG_WARNING(Service, "(STUBBED) nx2:gpu TIPC 22, returning a sub-session on nx2:gpu");
        ReturnRawSession(system, ctx, std::static_pointer_cast<INx2GpuSession>(shared_from_this()));
    }

    // Sub 101: (start u64, last u64, flags...) after CreateGpuAccessibleMemory's svc 0x80.
    void SubRegisterMemory(HLERequestContext& ctx) {
        const auto* b = ctx.CommandBuffer();
        const u64 start = u64{b[2]} | (u64{b[3]} << 32);
        const u64 last = u64{b[4]} | (u64{b[5]} << 32);
        LOG_INFO(Service, "nx2:gpu sub 101 start={:#x} last={:#x} [6]={:#x} [7]={:#x}", start,
                    last, b[6], b[7]);
        if (last > start) {
            Nvidia::Devices::Nx2GpuMemoryRegistry::Get().Register(start, last - start + 1);
            // Two-page regions hold queue semaphores: GPU releases go to the second page, waits
            // read the first (see video_core/nx2.h).
            if (last - start + 1 == 0x2000) {
                Tegra::Nx2AddSemaphoreRegion(start);
                LOG_INFO(Service, "nx2:gpu semaphore page {:#x} mirrored to {:#x}", start + 0x1000,
                         start);
            }
        }
        // nn::gpu::sdk::CreateGpuAccessibleMemory stores word 3 of the reply as the
        // GpuMemoryHandle that later map jobs (211) name.
        const u32 handle = m_next_memory_handle++;
        m_memory[handle] = {start, last - start + 1};
        TipcReplyWords(ctx, {handle});
    }

    // Jobs that complete through a job-slot event (word Slot of the request), like 211.
    template <size_t Slot>
    void SubSlotJob(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        std::string words_str;
        for (size_t i = 0; i < 18; ++i) {
            if (i > 0) {
                words_str += ' ';
            }
            words_str += std::format("{:08x}", w[2 + i]);
        }
        LOG_INFO(Service, "(STUBBED) nx2:gpu {} words {} slot={}", ctx.GetCommand(), words_str,
                 w[Slot]);
        EventFor(w[Slot])->Signal(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    // Sub 211: job submission (nn::gpu::sdk job tracking). Word 2 is a constant id; word 17 is
    // the job slot whose event (gpu TIPC 12) the SDK then waits on. Nothing runs here, so
    // complete it at once.
    void SubSubmitJob(HLERequestContext& ctx) {
        const auto* cmd_buf = ctx.CommandBuffer();
        const u32 slot = cmd_buf[17];
        const u64 a = u64{cmd_buf[3]} | (u64{cmd_buf[4]} << 32);
        const u64 b = u64{cmd_buf[5]} | (u64{cmd_buf[6]} << 32);
        const u64 c = u64{cmd_buf[8]} | (u64{cmd_buf[9]} << 32);
        // Words 3-4: first GPU address, 5-6: last, 7: memory handle, 8-9: offset into it.
        const u32 handle = cmd_buf[7];
        LOG_INFO(Service, "nx2:gpu 211 map [{:#x}, {:#x}] handle={} offset={:#x} slot={}", a, b,
                 handle, c, slot);
        if (const auto it = m_memory.find(handle); it != m_memory.end() && b > a) {
            const u64 cpu = it->second.address + c;
            Nvidia::Devices::Nx2GpuMemoryRegistry::Get().MapAt(a, cpu, b - a + 1);
        } else {
            LOG_WARNING(Service, "nx2:gpu 211 map of unknown handle {}", handle);
        }
        EventFor(slot)->Signal(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SubModifyKind(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        const u64 first = u64{w[3]} | (u64{w[4]} << 32);
        const u64 last = u64{w[5]} | (u64{w[6]} << 32);
        const u32 kind = w[7] & 0xff;
        LOG_DEBUG(Service, "nx2:gpu 212 modify [{:#x}, {:#x}] kind={:#x} fallback={:#x} slot={}",
                  first, last, kind, w[8] & 0xff, w[10]);
        EventFor(w[10])->Signal(system.Kernel());
        TipcReplyWords(ctx, {kind});
    }

    void SubUnmapJob(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        const u64 first = u64{w[3]} | (u64{w[4]} << 32);
        const u64 last = u64{w[5]} | (u64{w[6]} << 32);
        LOG_DEBUG(Service, "nx2:gpu 213 unmap [{:#x}, {:#x}] slot={}", first, last, w[8]);
        if (last > first) {
            Nvidia::Devices::Nx2GpuMemoryRegistry::Get().Unmap(first, last - first + 1);
        }
        EventFor(w[8])->Signal(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SubDestroyMemory(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        const u32 handle = w[2];
        if (const auto it = m_memory.find(handle); it != m_memory.end()) {
            const auto [address, size] = it->second;
            LOG_DEBUG(Service, "nx2:gpu 102 destroy handle={} [{:#x}, +{:#x}] slot={}", handle,
                      address, size, w[3]);
            if (size != 0) {
                Nvidia::Devices::Nx2GpuMemoryRegistry::Get().Unmap(address, size);
                Nvidia::Devices::Nx2GpuMemoryRegistry::Get().ReleaseBacking(address, size);
            }
            if (size == 0x2000) {
                Tegra::Nx2RemoveSemaphoreRegion(address);
            }
            m_memory.erase(it);
        } else {
            LOG_WARNING(Service, "nx2:gpu 102 destroy of unknown handle {}", handle);
        }
        EventFor(w[3])->Signal(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    // Sub 214: GPU-only memory [start, last] (words 3-6, job slot in word 11). The graphics
    // server backs it itself: the client never maps anything there but renders into it (title 4
    // keeps its render targets in six 300 MB regions at 0x81_0000_0000). Back each region with
    // kernel memory placed in the process's alias code region and map that for the GPU.
    void SubCreateGpuOnlyRegion(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        const u64 start = u64{w[3]} | (u64{w[4]} << 32);
        const u64 last = u64{w[5]} | (u64{w[6]} << 32);
        const u32 slot = w[11];
        if (last > start && ((last + 1 - start) % Core::Memory::YUZU_PAGESIZE) == 0) {
            const u64 size = last + 1 - start;
            if (const auto address = BackGpuOnlyRegion(size)) {
                Nvidia::Devices::Nx2GpuMemoryRegistry::Get().MapAt(start, *address, size);
                LOG_INFO(Service, "nx2:gpu 214 GPU-only region [{:#x}, {:#x}] backed at {:#x}",
                         start, last, *address);
            }
        } else {
            LOG_WARNING(Service, "nx2:gpu 214 unexpected region [{:#x}, {:#x}]", start, last);
        }
        EventFor(slot)->Signal(system.Kernel());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    std::optional<u64> BackGpuOnlyRegion(u64 size) {
        using Core::Memory::YUZU_PAGESIZE;
        auto& kernel_core = system.Kernel();
        auto* const process = system.ApplicationProcess();
        if (process == nullptr) {
            return std::nullopt;
        }
        auto pg = std::make_unique<Kernel::KPageGroup>(
            kernel_core, std::addressof(kernel_core.GetSystemSystemResource().GetBlockInfoManager()));
        // Not cleared: the GPU writes these before reading them, and touching 300 MB would commit
        // it on the host. Try SystemNonSecure / Applet pools first so we do not steal from the
        // game's ApplicationPool.
        bool allocated = false;
        for (const auto pool : {Kernel::KMemoryManager::Pool::SystemNonSecure,
                                Kernel::KMemoryManager::Pool::Applet,
                                Kernel::KMemoryManager::Pool::Application}) {
            if (R_SUCCEEDED(kernel_core.MemoryManager().AllocateAndOpen(
                    pg.get(), size / YUZU_PAGESIZE,
                    Kernel::KMemoryManager::EncodeOption(
                        pool, Kernel::KMemoryManager::Direction::FromBack)))) {
                allocated = true;
                break;
            }
        }
        if (!allocated) {
            LOG_ERROR(Service, "nx2:gpu 214 could not allocate {:#x} bytes", size);
            return std::nullopt;
        }
        auto& page_table = process->GetPageTable();
        const u64 region_start = GetInteger(page_table.GetAliasCodeRegionStart());
        const u64 region_size = page_table.GetAliasCodeRegionSize();
        constexpr u64 Alignment = 0x200000; // 2 MiB
        for (u64 offset = 0; offset + size <= region_size; offset += Alignment) {
            const Common::ProcessAddress address{region_start + offset};
            if (R_SUCCEEDED(page_table.MapPageGroup(address, *pg, Kernel::KMemoryState::IoMemory,
                                                    Kernel::KMemoryPermission::UserReadWrite))) {
                m_gpu_only_memory.push_back(std::move(pg));
                return GetInteger(address);
            }
        }
        LOG_ERROR(Service, "nx2:gpu 214 no room for {:#x} bytes in the alias code region "
                           "[{:#x}, +{:#x})", size, region_start, region_size);
        pg->Close(kernel_core);
        return std::nullopt;
    }

    std::vector<std::unique_ptr<Kernel::KPageGroup>> m_gpu_only_memory;

    struct GpuMemory {
        u64 address;
        u64 size;
    };

    KernelHelpers::ServiceContext m_context;
    std::array<Kernel::KEvent*, NumIds> m_events{};
    std::array<bool, 64> m_devices{};
    std::map<u32, GpuMemory> m_memory;
    u32 m_next_memory_handle{1};
};

// Native memory and images handed out by the Switch 2 graphics server (gsv 111/114/423/429/315),
// shared with the vi2 session so presents can find an image's memory. Command meanings follow
// the shared changes.patch (IGraphicsSupervisor), re-checked against title 4's SDK.
struct Nx2NativeObjects {
    struct Memory {
        u64 size{};
        u64 address{}; // CPU (== GPU) address once bound by 114/423
    };
    struct Image {
        u32 memory{};
        std::array<u32, 16> preference{};
        u64 size{};
        bool window_pool{};
    };
    std::mutex mutex;
    std::map<u32, Memory> memories;
    std::map<u32, Image> images;
    u32 next_memory{1};
    u32 next_image{1};
};

// Block-linear layout of an InterModuleUserImagePreference (16 words): word 2 holds
// (width - 1) | (height - 1) << 16, word 4 the block height (log2 of GOB rows), word 5 the row
// pitch in 64-byte GOBs. Formula from changes.patch (native_image_layout.h); preferences outside
// it fall back to 4 bytes per pixel and 128-row blocks.
struct Nx2ImageLayout {
    u32 width;
    u32 height;
    u64 pitch;
    u32 block_height_log2;
    u64 size;
    u64 alignment;
};

inline Nx2ImageLayout Nx2CalculateImageLayout(const u32* preference) {
    Nx2ImageLayout layout{};
    layout.width = (preference[2] & 0xFFFF) + 1;
    layout.height = (preference[2] >> 16) + 1;
    const u64 pitch_gobs = preference[5];
    const u32 block_height_log2 = preference[4];
    if (block_height_log2 <= 8 && pitch_gobs >= (u64{layout.width} * 4 + 63) / 64 &&
        pitch_gobs <= 4096) {
        layout.pitch = pitch_gobs * 64;
        layout.block_height_log2 = block_height_log2;
    } else {
        layout.pitch = Common::AlignUp(u64{layout.width} * 4, 64);
        layout.block_height_log2 = 7;
    }
    const u64 block_rows = u64{1} << layout.block_height_log2;
    layout.size = layout.pitch * Common::AlignUp(u64{layout.height}, block_rows);
    layout.alignment = std::max<u64>(64 * block_rows, 0x1000);
    return layout;
}

// Placeholder for the Switch 2 display session (nn::vi2::sdk), handed out by gsv 305. Reply
// shapes read from the SDK: TIPC 1 -> u32 < 0x400, TIPC 3 -> u32 < 8, TIPC 11/31(u32) and
// 32(handle, u32) -> one handle at word 3 then the result. Handles returned here are events (a
// guess); everything else is auto-stubbed through the nx2: prefix.
class INx2Vi2Session final : public ServiceFramework<INx2Vi2Session> {
public:
    explicit INx2Vi2Session(Core::System& system_, std::shared_ptr<Nx2NativeObjects> objects_)
        : ServiceFramework{system_, "nx2:vi2"}, m_context{system_, "nx2:vi2"},
          m_objects{std::move(objects_)} {
        RegisterHandlersTipc({
            {0, &INx2Vi2Session::Handshake, "Handshake"},
            {1, &INx2Vi2Session::ReturnOne, "Unknown1"},
            {3, &INx2Vi2Session::ReturnOne, "GetDisplayCount (guessed)"},
            {5, &INx2Vi2Session::GetTimestampRecorderCount, "GetTimestampRecorderCount (guessed)"},
            {11, &INx2Vi2Session::ReturnEvent, "Unknown11"},
            {31, &INx2Vi2Session::AcquireNativeEvent, "AcquireNativeEvent"},
            {32, &INx2Vi2Session::DuplicateNativeEvent, "DuplicateNativeEvent"},
            {103, &INx2Vi2Session::QueryDisplayRefreshRate, "QueryDisplayRefreshRate"},
            {104, &INx2Vi2Session::QueryDisplayHdrStatus, "QueryDisplayHdrStatus"},
            {105, &INx2Vi2Session::QueryDisplayHdrPaperWhite, "QueryDisplayHdrPaperWhite"},
            {106, &INx2Vi2Session::GetBufferMaxResolution, "GetBufferMaxResolution"},
            {212, &INx2Vi2Session::GetLayerBufferBits, "GetLayerBufferBits"},
            {228, &INx2Vi2Session::CancelClientBuffer, "CancelClientBuffer"},
            {229, &INx2Vi2Session::PresentClientBuffer, "PresentClientBuffer"},
            {261, &INx2Vi2Session::BindVsync, "BindDisplayVsyncEvent"},
            {262, &INx2Vi2Session::UnbindNativeEvent, "UnbindDisplayVsyncEvent"},
            {263, &INx2Vi2Session::BindChange, "BindDisplayRefreshRateEvent"},
            {264, &INx2Vi2Session::UnbindNativeEvent, "UnbindDisplayRefreshRateEvent"},
            {265, &INx2Vi2Session::BindChange, "BindDisplayHdrStatusEvent"},
            {266, &INx2Vi2Session::UnbindNativeEvent, "UnbindDisplayHdrStatusEvent"},
            {267, &INx2Vi2Session::BindChange, "BindDisplayHdrPaperWhiteEvent"},
            {268, &INx2Vi2Session::UnbindNativeEvent, "UnbindDisplayHdrPaperWhiteEvent"},
            {269, &INx2Vi2Session::BindChange, "BindDisplayBufferMaxResolutionEvent"},
            {270, &INx2Vi2Session::UnbindNativeEvent, "UnbindDisplayBufferMaxResolutionEvent"},
            {271, &INx2Vi2Session::BindChange, "BindDisplayModeCapabilityEvent"},
            {272, &INx2Vi2Session::UnbindNativeEvent, "UnbindDisplayModeCapabilityEvent"},
            // Quiet zeroed replies for known Switch 2 display/layer setup commands.
            {13, &INx2Vi2Session::StubReplyZeroed, "Unknown13"},
            {51, &INx2Vi2Session::StubReplyZeroed, "Unknown51"},
            {101, &INx2Vi2Session::StubReplyZeroed, "Unknown101"},
            {102, &INx2Vi2Session::StubReplyZeroed, "Unknown102"},
            {107, &INx2Vi2Session::StubReplyZeroed, "Unknown107"},
            {108, &INx2Vi2Session::StubReplyZeroed, "Unknown108"},
            {111, &INx2Vi2Session::StubReplyZeroed, "Unknown111"},
            {112, &INx2Vi2Session::StubReplyZeroed, "Unknown112"},
            {113, &INx2Vi2Session::StubReplyZeroed, "Unknown113"},
            {201, &INx2Vi2Session::StubReplyZeroed, "Unknown201"},
            {202, &INx2Vi2Session::StubReplyZeroed, "Unknown202"},
            {230, &INx2Vi2Session::StubReplyZeroed, "Unknown230"},
            {231, &INx2Vi2Session::StubReplyZeroed, "Unknown231"},
            {232, &INx2Vi2Session::StubReplyZeroed, "Unknown232"},
            {251, &INx2Vi2Session::StubReplyZeroed, "Unknown251"},
            {252, &INx2Vi2Session::StubReplyZeroed, "Unknown252"},
            {301, &INx2Vi2Session::StubReplyZeroed, "Unknown301"},
            {302, &INx2Vi2Session::StubReplyZeroed, "Unknown302"},
            {351, &INx2Vi2Session::StubReplyZeroed, "Unknown351"},
            {352, &INx2Vi2Session::StubReplyZeroed, "Unknown352"},
            {353, &INx2Vi2Session::StubReplyZeroed, "Unknown353"},
            {354, &INx2Vi2Session::StubReplyZeroed, "Unknown354"},
            {355, &INx2Vi2Session::StubReplyZeroed, "Unknown355"},
            {356, &INx2Vi2Session::StubReplyZeroed, "Unknown356"},
            {358, &INx2Vi2Session::StubReplyZeroed, "Unknown358"},
            {359, &INx2Vi2Session::StubReplyZeroed, "Unknown359"},
        });
    }

    ~INx2Vi2Session() override {
        {
            std::scoped_lock lk{m_vsync->mutex};
            m_vsync->events.clear();
        }
        if (m_vsync_timer) {
            system.CoreTiming().UnscheduleEvent(m_vsync_timer);
        }
        for (auto* event : m_events) {
            m_context.CloseEvent(event);
        }
    }

private:
    void Handshake(HLERequestContext& ctx) {
        TipcHandshake(ctx, "nx2:vi2");
    }

    void StubReplyZeroed(HLERequestContext& ctx) {
        static constexpr std::array<u32, 32> zeros{};
        TipcReplyWords(ctx, zeros);
    }

    // 229(u64 client token, u32 slot, u32 image, ...fences): result, u64 mask of buffer slots the
    // client may acquire next, four zero words.
    void PresentClientBuffer(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        ++m_presents;
        if (m_presents <= 16 || (m_presents % 300) == 0) {
            LOG_WARNING(Service, "nx2:vi2 229 present #{} token={:#x} slot={} image={}", m_presents,
                        u64{w[2]} | (u64{w[3]} << 32), w[4], w[5]);
        }
        WaitForFrame(~w[22], w[24]);
        Composite(w[5], ~w[22], w[24], w[7]);
        constexpr u64 AvailableSlots = 0x7fff;
        TipcReplyWords(ctx, {static_cast<u32>(AvailableSlots),
                             static_cast<u32>(AvailableSlots >> 32), 0, 0, 0, 0});
    }

    u32 m_wait_timeouts{};

    void WaitForFrame(u32 fence_id, u32 fence_value) {
        if (fence_id >= 192) {
            return;
        }
        auto& syncpoints = system.Host1x().GetSyncpointManager();
        const auto start = std::chrono::steady_clock::now();
        const auto deadline = start + std::chrono::milliseconds{4000};
        while (!syncpoints.IsReadyGuest(fence_id, fence_value) &&
               std::chrono::steady_clock::now() < deadline && system.IsPoweredOn()) {
            std::this_thread::sleep_for(std::chrono::microseconds{200});
        }
        if (!syncpoints.IsReadyGuest(fence_id, fence_value) && m_wait_timeouts++ < 64) {
            LOG_WARNING(Service, "nx2:vi2 present #{} fence ({}, {}) not reached in 4 s (now {})",
                        m_presents, fence_id, fence_value, syncpoints.GetGuestSyncpointValue(fence_id));
        }
        if (m_presents <= 8 || (m_presents % 600) == 0) {
            LOG_INFO(Service, "nx2:vi2 present #{} fence ({}, {}) now {}, waited {} us",
                     m_presents, fence_id, fence_value, syncpoints.GetGuestSyncpointValue(fence_id),
                     std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count());
        }
    }

    void Composite(u32 image_id, u32 fence_id, u32 fence_value, u32 present_size = 0) {
        u64 address{};
        Nx2ImageLayout layout{};
        u32 image_format{};
        {
            std::scoped_lock lk{m_objects->mutex};
            const auto image = m_objects->images.find(image_id);
            if (image == m_objects->images.end()) {
                return;
            }
            const auto memory = m_objects->memories.find(image->second.memory);
            if (memory == m_objects->memories.end() || memory->second.address == 0) {
                return;
            }
            address = memory->second.address;
            layout = Nx2CalculateImageLayout(image->second.preference.data());
            image_format = image->second.preference[6];
        }
        const auto device =
            Nvidia::Devices::Nx2GpuMemoryRegistry::Get().DeviceAddressOf(address, layout.size);
        if (!device) {
            LOG_WARNING(Service, "nx2:vi2 present: image {} at {:#x} has no device mapping",
                        image_id, address);
            return;
        }
        u32 crop_width = layout.width;
        u32 crop_height = layout.height;
        const u32 shown_width = (present_size & 0xffffU) + 1;
        const u32 shown_height = (present_size >> 16) + 1;
        const bool has_shown_size = present_size != 0 && shown_width >= 64 && shown_height >= 64 &&
                                    shown_width <= layout.width && shown_height <= layout.height;
        if (has_shown_size) {
            crop_width = shown_width;
            crop_height = shown_height;
        } else if (!Settings::IsDockedMode() && crop_width > 1920 && crop_height > 1080) {
            crop_width = 1920;
            crop_height = 1080;
        }
        if (m_presents <= 6 || (m_presents % 300) == 0) {
            LOG_WARNING(Service,
                        "nx2:vi2 Composite #{}: img={} addr={:#x} dev={:#x} layout={}x{} fmt={} w7={:#x} crop={}x{}",
                        m_presents, image_id, address, *device, layout.width, layout.height,
                        image_format, present_size, crop_width, crop_height);
        }
        std::vector<Tegra::FramebufferConfig> layers{Tegra::FramebufferConfig{
            .address = *device,
            .offset = 0,
            .width = layout.width,
            .height = layout.height,
            .stride = static_cast<u32>(layout.pitch / 4),
            .pixel_format = image_format == 4 ? android::PixelFormat::Rgba1010102
                                              : android::PixelFormat::Rgba8888,
            .transform_flags = android::BufferTransformFlags::Unset,
            .crop_rect = {0, 0, static_cast<int>(crop_width), static_cast<int>(crop_height)},
            .blending = Tegra::BlendMode::Opaque,
            .native_tiling = true,
        }};
        std::vector<Nvidia::NvFence> fences;
        if (fence_id < 192) {
            fences.push_back({static_cast<s32>(fence_id), fence_value});
        }
        system.GPU().RequestComposite(std::move(layers), std::move(fences));
        CaptureFrame();
        system.SpeedLimiter().DoSpeedLimiting(system.CoreTiming().GetGlobalTimeUs());
        system.GetPerfStats().EndSystemFrame();
        system.GetPerfStats().BeginSystemFrame();
    }

    void CaptureFrame() {
        static const char* const directory = std::getenv("EDEN_NX2_FRAME_CAPTURE");
        const bool probe_pixels = (m_presents == 60 || m_presents == 300 || m_presents == 600 ||
                                   m_presents == 1200 || m_presents == 2400);
        if ((directory == nullptr || (m_presents % 120) != 1) && !probe_pixels) {
            return;
        }
        if (m_capture_pending->exchange(true)) {
            return;
        }
        const u32 present_id = m_presents;
        const bool write_tga = (directory != nullptr && (present_id % 120) == 1);
        const auto layout = Layout::DefaultFrameLayout(write_tga ? 1280 : 320, write_tga ? 720 : 180);
        auto pixels = std::make_shared<std::vector<u8>>(layout.width * layout.height * 4);
        const std::string path =
            write_tga ? std::format("{}/nx2-frame-{:05}.tga", directory, present_id) : std::string{};
        auto pending = m_capture_pending;
        system.GPU().Renderer().RequestScreenshot(
            pixels->data(),
            [pixels, layout, path, pending, present_id, write_tga](bool invert_y) {
                std::size_t non_black = 0;
                u32 max_rgb = 0;
                const std::size_t total = static_cast<std::size_t>(layout.width) * layout.height;
                for (std::size_t i = 0; i < total; ++i) {
                    const u8 r = (*pixels)[i * 4 + 0];
                    const u8 g = (*pixels)[i * 4 + 1];
                    const u8 b = (*pixels)[i * 4 + 2];
                    const u32 m = (std::max)({static_cast<u32>(r), static_cast<u32>(g), static_cast<u32>(b)});
                    if (m > max_rgb) {
                        max_rgb = m;
                    }
                    if (m > 2) {
                        ++non_black;
                    }
                }
                LOG_WARNING(Service, "nx2:vi2 frame #{} pixel probe: non_black={}/{} max_rgb={}",
                            present_id, non_black, total, max_rgb);
                if (write_tga) {
                    std::array<u8, 18> header{};
                    header[2] = 2; // uncompressed true colour
                    header[12] = static_cast<u8>(layout.width);
                    header[13] = static_cast<u8>(layout.width >> 8);
                    header[14] = static_cast<u8>(layout.height);
                    header[15] = static_cast<u8>(layout.height >> 8);
                    header[16] = 32;
                    header[17] = invert_y ? 0x08 : 0x28;
                    if (std::FILE* file = std::fopen(path.c_str(), "wb")) {
                        std::fwrite(header.data(), 1, header.size(), file);
                        std::fwrite(pixels->data(), 1, pixels->size(), file);
                        std::fclose(file);
                    }
                }
                pending->store(false);
            },
            layout);
    }

    // 228(u64 token, u32 slot): return a buffer without presenting it.
    void CancelClientBuffer(HLERequestContext& ctx) {
        constexpr u64 AvailableSlots = 0x7fff;
        TipcReplyWords(ctx, {static_cast<u32>(AvailableSlots),
                             static_cast<u32>(AvailableSlots >> 32), 0, 0, 0, 0});
    }

    // 212(u64 layer): u64 bits of the layer's buffer slots that are ready.
    void GetLayerBufferBits(HLERequestContext& ctx) {
        constexpr u64 AvailableSlots = 0x7fff;
        TipcReplyWords(ctx,
                       {static_cast<u32>(AvailableSlots), static_cast<u32>(AvailableSlots >> 32)});
    }

    void QueryDisplayRefreshRate(HLERequestContext& ctx) {
        TipcReplyWords(ctx, {60, 0});
    }

    void QueryDisplayHdrPaperWhite(HLERequestContext& ctx) {
        TipcReplyWords(ctx, {203});
    }

    void QueryDisplayHdrStatus(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        constexpr u64 DefaultDisplay = 0x00746c7561666564ULL; // "default"
        if (packet[0] == 0x78 && packet[1] == 2) {
            const Result result =
                name == DefaultDisplay ? ResultSuccess : Result{ErrorModule::VI, 7};
            const u64 format = name == DefaultDisplay ? 1ULL : 0ULL;
            IPC::ResponseBuilder rb{ctx, 5};
            rb.PushRaw<u32>(result.raw);
            rb.Push<u64>(format);
            rb.Push<u32>(0);
            return;
        }
        TipcReplyWords(ctx, {0, 0, 0});
    }

    void GetBufferMaxResolution(HLERequestContext& ctx) {
        const auto* packet = ctx.CommandBuffer();
        const u64 name = u64{packet[2]} | (u64{packet[3]} << 32);
        constexpr u64 DefaultDisplay = 0x00746c7561666564ULL; // "default"
        static const char* forced = std::getenv("EDEN_VI2_MAX_RESOLUTION_MODE");
        const u64 default_mode = forced ? std::strtoull(forced, nullptr, 0)
                                        : (Settings::IsDockedMode() ? 4ULL : 2ULL);
        if (packet[0] == 0x7a && packet[1] == 2) {
            const Result result =
                name == DefaultDisplay ? ResultSuccess : Result{ErrorModule::VI, 7};
            const u64 mode = name == DefaultDisplay ? default_mode : 0ULL;
            IPC::ResponseBuilder rb{ctx, 4};
            rb.PushRaw<u32>(result.raw);
            rb.Push<u64>(mode);
            return;
        }
        TipcReplyWords(ctx, {static_cast<u32>(default_mode), static_cast<u32>(default_mode >> 32)});
    }

    void ReturnOne(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:vi2 TIPC {}, returning 1", ctx.GetCommand());
        TipcReplyWords(ctx, {1});
    }

    // Must be < 8; nn::vi::detail::LayerHistory creates 7 timestamp recorders per layer.
    void GetTimestampRecorderCount(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:vi2 TIPC 5, returning 7");
        TipcReplyWords(ctx, {7});
    }

    void ReturnEvent(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:vi2 TIPC {} [2]={:#x} [3]={:#x}, returning an event",
                    ctx.GetCommand(), ctx.CommandBuffer()[2], ctx.CommandBuffer()[3]);
        auto* event = m_context.CreateEvent(std::format("nx2:vi2:Event{}", m_events.size()));
        m_events.push_back(event);
        IPC::ResponseBuilder rb{ctx, 2, 1, 0};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, event->GetReadableEvent());
    }

    void AcquireNativeEvent(HLERequestContext& ctx) {
        const u32 index = ctx.CommandBuffer()[2];
        if (index >= m_native_events.size()) {
            LOG_ERROR(Service, "nx2:vi2 31 index {} out of range", index);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown);
            return;
        }
        LOG_INFO(Service, "nx2:vi2 31 native event {}", index);
        ReplyNativeEvent(ctx, index);
    }

    void DuplicateNativeEvent(HLERequestContext& ctx) {
        const u32* words = ctx.CommandBuffer();
        u32 data = 2;
        if (words[1] >> 31) {
            const u32 descriptor = words[2];
            data = 3 + ((descriptor >> 1) & 0xf) + ((descriptor >> 5) & 0xf) +
                   (descriptor & 1 ? 2 : 0);
        }
        const u32 index = words[data];
        if (index >= m_native_events.size()) {
            LOG_ERROR(Service, "nx2:vi2 32 index {} out of range", index);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown);
            return;
        }
        LOG_INFO(Service, "nx2:vi2 32 native event {}", index);
        ReplyNativeEvent(ctx, index);
    }

    void BindVsync(HLERequestContext& ctx) {
        const u32 index = ctx.CommandBuffer()[4];
        LOG_INFO(Service, "nx2:vi2 261 native event {} -> vsync", index);
        if (index < m_native_events.size() && m_native_events[index]) {
            SetVsyncSlot(index, true);
        }
        TipcReplyWords(ctx, {});
    }

    void BindChange(HLERequestContext& ctx) {
        const u32 index = ctx.CommandBuffer()[4];
        LOG_INFO(Service, "nx2:vi2 {} native event {} -> change notification", ctx.GetCommand(),
                 index);
        if (index < m_native_events.size() && m_native_events[index]) {
            SetVsyncSlot(index, false);
            m_native_events[index]->Signal(system.Kernel());
        }
        TipcReplyWords(ctx, {});
    }

    void UnbindNativeEvent(HLERequestContext& ctx) {
        const u32 index = ctx.CommandBuffer()[4];
        LOG_INFO(Service, "nx2:vi2 {} native event {} unbound", ctx.GetCommand(), index);
        if (index < m_native_events.size() && m_native_events[index]) {
            SetVsyncSlot(index, false);
        }
        TipcReplyWords(ctx, {});
    }

    void ReplyNativeEvent(HLERequestContext& ctx, u32 index) {
        auto*& event = m_native_events[index];
        if (!event) {
            event = m_context.CreateEvent(std::format("nx2:vi2:Native{}", index));
            m_events.push_back(event);
            SetVsyncSlot(index, true);
            StartVsyncTimer();
        }
        IPC::ResponseBuilder rb{ctx, 2, 1, 0};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, event->GetReadableEvent());
    }

    void SetVsyncSlot(u32 index, bool on) {
        auto* event = m_native_events[index];
        std::scoped_lock lk{m_vsync->mutex};
        auto& list = m_vsync->events;
        const auto it = std::find(list.begin(), list.end(), event);
        if (on && it == list.end()) {
            list.push_back(event);
        } else if (!on && it != list.end()) {
            list.erase(it);
        }
    }

    void StartVsyncTimer() {
        if (!m_vsync_timer) {
            m_vsync_timer = Core::Timing::CreateEvent(
                "nx2:vi2 vsync",
                [vsync_state = m_vsync, &kernel = system.Kernel()](
                    s64, std::chrono::nanoseconds) -> std::optional<std::chrono::nanoseconds> {
                    std::scoped_lock lk{vsync_state->mutex};
                    for (auto* vsync : vsync_state->events) {
                        vsync->Signal(kernel);
                    }
                    return std::nullopt;
                });
            static const std::chrono::nanoseconds VblankInterval{[] {
                const char* text = std::getenv("EDEN_NX2_VSYNC_HZ");
                const long long hz =
                    text ? (std::max)(std::strtoll(text, nullptr, 10), 1LL) : 60LL;
                return 1'000'000'000LL / hz;
            }()};
            system.CoreTiming().ScheduleLoopingEvent(VblankInterval, VblankInterval, m_vsync_timer);
        }
    }

    struct VsyncState {
        std::mutex mutex;
        std::vector<Kernel::KEvent*> events;
    };

    KernelHelpers::ServiceContext m_context;
    std::shared_ptr<Nx2NativeObjects> m_objects;
    std::vector<Kernel::KEvent*> m_events;
    std::array<Kernel::KEvent*, 8> m_native_events{};
    std::shared_ptr<VsyncState> m_vsync = std::make_shared<VsyncState>();
    std::shared_ptr<Core::Timing::EventType> m_vsync_timer;
    u64 m_presents{};
    std::shared_ptr<std::atomic<bool>> m_capture_pending =
        std::make_shared<std::atomic<bool>>(false);
};

// Placeholder for the Switch 2 graphics-system server that nn::gsv::BootWithRemoteMemory talks
// to over TIPC. Nothing behind it is emulated: commands succeed and are logged, so boot can show
// what comes next. Commands 202 and 1402 hand out the syncpt and nvsrv sessions the SDK uses.
class INx2GraphicsSystemSession final : public ServiceFramework<INx2GraphicsSystemSession> {
public:
    explicit INx2GraphicsSystemSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:gsv"}, m_context{system_, "nx2:gsv"} {
        RegisterHandlersTipc({
            {4, &INx2GraphicsSystemSession::Stub, "Boot (Switch 2, guessed)"},
            {5, &INx2GraphicsSystemSession::Stub, "Unknown5"},
            {101, &INx2GraphicsSystemSession::Stub, "Unknown101"},
            {202, &INx2GraphicsSystemSession::OpenSyncptSession, "OpenSyncptSession (guessed)"},
            {204, &INx2GraphicsSystemSession::AttachSyncptMemory, "AttachSyncptMemory (guessed)"},
            {305, &INx2GraphicsSystemSession::OpenViSession, "OpenViSession (guessed)"},
            {497, &INx2GraphicsSystemSession::OpenGpuSession, "OpenGpuSession (guessed)"},
            {111, &INx2GraphicsSystemSession::CreateNativeMemory, "CreateNativeMemory"},
            {112, &INx2GraphicsSystemSession::DestroyNativeMemory, "DestroyNativeMemory"},
            {114, &INx2GraphicsSystemSession::BindNativeMemory, "BindNativeMemory"},
            {314, &INx2GraphicsSystemSession::ConfigureWindowImagePool, "DestroyNativeWindowAliasImagePool"},
            {315, &INx2GraphicsSystemSession::ConfigureWindowImagePool, "CreateNativeWindowAliasImagePool"},
            {423, &INx2GraphicsSystemSession::BindNativeGpuMemory, "BindNativeGpuMemory"},
            {429, &INx2GraphicsSystemSession::CreateNativeImage, "CreateNativeImage"},
            {430, &INx2GraphicsSystemSession::DestroyNativeImage, "DestroyNativeImage"},
            {435, &INx2GraphicsSystemSession::CalculateNativeImageSize, "CalculateNativeImageSize"},
            {1402, &INx2GraphicsSystemSession::OpenNvsrvSession, "OpenNvsrvSession (guessed)"},
            {1510, &INx2GraphicsSystemSession::CalculateRequirements, "CalculateRequirements"},
        });
    }

    ~INx2GraphicsSystemSession() override {
        if (m_syncpt_memory != nullptr) {
            system.Host1x().GetSyncpointManager().SetGuestMirror({});
        }
        if (m_syncpt_poll) {
            system.CoreTiming().UnscheduleEvent(m_syncpt_poll);
        }
        for (auto* event : m_syncpt_events) {
            if (event != nullptr) {
                m_context.CloseEvent(event);
            }
        }
    }

private:
    // Index of the first raw data word of a TIPC request: header (2 words), then an optional
    // special header with pid and copy/move handle words.
    static size_t TipcDataOffset(const u32* cmd_buf) {
        if ((cmd_buf[1] & 0x80000000) == 0) {
            return 2;
        }
        const u32 desc = cmd_buf[2];
        return 3 + ((desc & 1) ? 2 : 0) + ((desc >> 1) & 0xF) + ((desc >> 5) & 0xF);
    }

    void Stub(HLERequestContext& ctx) {
        const auto* cmd_buf = ctx.CommandBuffer();
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC {} [2]={:#x} [3]={:#x} [4]={:#x} [5]={:#x}",
                    ctx.GetCommand(), cmd_buf[2], cmd_buf[3], cmd_buf[4], cmd_buf[5]);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void OpenSyncptSession(HLERequestContext& ctx) {
        // Used by nn::syncpt::sdk::InitializeEx; handle at word 3, result at word 4.
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 202, returning a nx2:syncpt session");
        ReturnRawSession(system, ctx, std::make_shared<INx2SyncptSession>(system));
    }

    // nn::syncpt::sdk::InitializeEx sends two transfer memories (0x1000 bytes RW, then 0x2000
    // bytes the client maps read-only) and expects the server to have written a header into the
    // second one: u32 version (must be 1) at +0, u32 base id at +4, u32 count at +8 (the client
    // requires ((count + 0x40) >> 6) - 1 < 0x40), then offsets of: the u64 syncpoint values (RO,
    // +0xc), the RW "armed" summary word (+0x10), the RO summary word (+0x14), the RW armed bits
    // (+0x18) and the RO acknowledge bits (+0x1c).
    // To wait, the client makes its RW bit differ from the RO bit, signals the first returned
    // event (it attaches that handle as writable), and waits on the second until the server makes
    // the bits equal again; it then rereads the value and rearms if the threshold isn't reached.
    // Reply: the two handles (words 3 and 4), then the result.
    static constexpr u32 SyncptValuesOffset = 0x40;   // RO, u64 per syncpoint
    static constexpr u32 SyncptRoSummaryOffset = 0x1040;
    static constexpr u32 SyncptRoBitsOffset = 0x1100; // RO, up to 0x40 u64 words
    static constexpr u32 SyncptRwSummaryOffset = 0x0;
    static constexpr u32 SyncptRwBitsOffset = 0x100;  // RW, up to 0x40 u64 words
    static constexpr u32 HostSyncpoints = 192;        // Tegra::Host1x::SyncpointManager

    void AttachSyncptMemory(HLERequestContext& ctx) {
        // RequestParser doesn't skip TIPC handle words, so read the raw data words.
        const auto* cmd_buf = ctx.CommandBuffer();
        const size_t data = TipcDataOffset(cmd_buf);
        const u32 version_requested = cmd_buf[data];
        const u32 max_syncpoints = std::min<u32>(cmd_buf[data + 1], (0x1000 - 0x40) / 8);
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 204, version={} max_syncpoints={:#x}",
                    version_requested, max_syncpoints);

        auto rw = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(ctx.GetCopyHandle(0));
        auto ro = ctx.GetObjectFromHandle<Kernel::KTransferMemory>(ctx.GetCopyHandle(1));
        if (rw.IsNotNull() && ro.IsNotNull() && ro->GetSize() >= 0x2000) {
            m_syncpt_memory = &ro->GetOwner()->GetMemory();
            m_syncpt_rw = GetInteger(rw->GetSourceAddress());
            m_syncpt_ro = GetInteger(ro->GetSourceAddress());
            m_syncpt_count = max_syncpoints;
            auto& memory = *m_syncpt_memory;
            memory.Write32(m_syncpt_ro + 0x00, 1); // version
            memory.Write32(m_syncpt_ro + 0x04, 0); // base id (guess)
            memory.Write32(m_syncpt_ro + 0x08, max_syncpoints);
            memory.Write32(m_syncpt_ro + 0x0c, SyncptValuesOffset);
            memory.Write32(m_syncpt_ro + 0x10, SyncptRwSummaryOffset);
            memory.Write32(m_syncpt_ro + 0x14, SyncptRoSummaryOffset);
            memory.Write32(m_syncpt_ro + 0x18, SyncptRwBitsOffset);
            memory.Write32(m_syncpt_ro + 0x1c, SyncptRoBitsOffset);
            UpdateSyncptValues();
            // Mirror each guest increment as it happens, before the waits it meets are answered:
            // the client's TryWaitNativeSync reads this page, and a client woken by an answered
            // wait (nx2:syncpt 201) before the next poll would otherwise read the stale value.
            system.Host1x().GetSyncpointManager().SetGuestMirror(
                [memory = m_syncpt_memory, values = m_syncpt_ro + SyncptValuesOffset,
                 count = std::min(m_syncpt_count, HostSyncpoints)](u32 id, u32 value) {
                    if (id < count) {
                        memory->Write64(values + u64{id} * 8, value);
                    }
                });
            if (!m_syncpt_poll) {
                m_syncpt_poll = Core::Timing::CreateEvent(
                    "nx2:gsv:SyncptPoll",
                    [this](s64, std::chrono::nanoseconds) -> std::optional<std::chrono::nanoseconds> {
                        PollSyncpts();
                        return std::nullopt;
                    });
                system.CoreTiming().ScheduleLoopingEvent(SyncptPollInterval, SyncptPollInterval,
                                                         m_syncpt_poll);
            }
        } else {
            LOG_ERROR(Service, "nx2:gsv TIPC 204: missing syncpt transfer memory");
        }

        if (m_syncpt_events[0] == nullptr) {
            m_syncpt_events[0] = m_context.CreateEvent("nx2:gsv:SyncptRequest");
            m_syncpt_events[1] = m_context.CreateEvent("nx2:gsv:SyncptNotify");
        }
        IPC::ResponseBuilder rb{ctx, 2, 2, 0};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, *m_syncpt_events[0]); // signalled by the client
        rb.PushCopyObjects(ctx, m_syncpt_events[1]->GetReadableEvent());
    }

    // Mirrors Eden's guest syncpoint values into the shared RO page. Returns true if any changed.
    bool UpdateSyncptValues() {
        auto& manager = system.Host1x().GetSyncpointManager();
        bool changed = false;
        manager.WithGuestValuesStable([&] {
            for (u32 id = 0; id < std::min(m_syncpt_count, HostSyncpoints); ++id) {
                const u64 value = manager.GetGuestSyncpointValue(id);
                const u64 address = m_syncpt_ro + SyncptValuesOffset + id * 8;
                if (m_syncpt_memory->Read64(address) != value) {
                    m_syncpt_memory->Write64(address, value);
                    changed = true;
                }
            }
        });
        return changed;
    }

    // Answers every armed wait: the value page is current, so the client rechecks its threshold
    // and rearms if it still has to wait. Acknowledging without a value change costs the client a
    // wakeup per poll, which keeps waits whose syncpoint we never see from hanging.
    void PollSyncpts() {
        if (m_syncpt_memory == nullptr) {
            return;
        }
        UpdateSyncptValues();
        bool acked = false;
        const u32 words = (m_syncpt_count + 63) / 64;
        for (u32 w = 0; w < words; ++w) {
            const u64 rw_bits = m_syncpt_memory->Read64(m_syncpt_rw + SyncptRwBitsOffset + w * 8);
            const u64 ro_address = m_syncpt_ro + SyncptRoBitsOffset + w * 8;
            if (m_syncpt_memory->Read64(ro_address) != rw_bits) {
                m_syncpt_memory->Write64(ro_address, rw_bits);
                acked = true;
            }
        }
        m_syncpt_memory->Write64(m_syncpt_ro + SyncptRoSummaryOffset,
                                 m_syncpt_memory->Read64(m_syncpt_rw + SyncptRwSummaryOffset));
        if (acked) {
            m_syncpt_events[1]->Signal(system.Kernel());
        }
    }

    // nn::gsv::shim::CalculateRequirements(u64* size, u64* alignment, preference, u32): the 0x40-byte
    // InterModuleUserImagePreference (words 2..17) and a u32 (word 18). A block-linear preference
    // (MakeInterModuleBlockUserImagePreference) holds kind 2 at word 3, (width - 1) | (height - 1)
    // << 16 at word 4, two u32s at words 6-7 and the pixel format at word 8. Reply: result, u64
    // size, u64 alignment; the client raises the alignment to at least 4 KiB and rounds the size.
    void CalculateRequirements(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        const auto layout = Nx2CalculateImageLayout(w + 2);
        LOG_DEBUG(Service, "nx2:gsv 1510 kind={} {}x{} block={} pitch={:#x} format={} -> size={:#x} align={:#x}",
                  w[3], layout.width, layout.height, layout.block_height_log2, layout.pitch, w[8],
                  layout.size, layout.alignment);
        TipcReplyWords(ctx, {static_cast<u32>(layout.size), static_cast<u32>(layout.size >> 32),
                             static_cast<u32>(layout.alignment),
                             static_cast<u32>(layout.alignment >> 32)});
    }

    // 435(preference[16], u32 count): result, u64 size of a GPU native image.
    void CalculateNativeImageSize(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        const u64 size = Nx2CalculateImageLayout(w + 2).size * std::max<u32>(w[18], 1);
        LOG_DEBUG(Service, "nx2:gsv 435 count={} -> size={:#x}", w[18], size);
        TipcReplyWords(ctx, {static_cast<u32>(size), static_cast<u32>(size >> 32)});
    }

    static u64 Pair(const u32* w, size_t i) {
        return u64{w[i]} | (u64{w[i + 1]} << 32);
    }

    // 111(u8 mode at word 2, u64 size): result, u32 native memory id.
    void CreateNativeMemory(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        const u64 size = Pair(w, 3);
        std::scoped_lock lk{m_objects->mutex};
        const u32 id = m_objects->next_memory++;
        m_objects->memories[id] = {size, 0};
        LOG_INFO(Service, "nx2:gsv 111 CreateNativeMemory mode={} size={:#x} -> id {}", w[2] & 0xFF,
                 size, id);
        TipcReplyWords(ctx, {id});
    }

    void DestroyNativeMemory(HLERequestContext& ctx) {
        const u32 id = ctx.CommandBuffer()[2];
        std::scoped_lock lk{m_objects->mutex};
        m_objects->memories.erase(id);
        LOG_DEBUG(Service, "nx2:gsv 112 DestroyNativeMemory id {}", id);
        TipcReplyWords(ctx, {});
    }

    // 114(u32 id, u64 address, u64 size, u64 flags, u64 offset): back native memory with process
    // memory.
    void BindNativeMemory(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        BindMemory(w[2], Pair(w, 3), Pair(w, 5), "114");
        TipcReplyWords(ctx, {});
    }

    // 423(u32 id, u64 address, u64 size, u32 parent GpuMemoryHandle, u64 offset, ...): back
    // native memory with part of GPU-accessible memory.
    void BindNativeGpuMemory(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        LOG_DEBUG(Service, "nx2:gsv 423 parent={} offset={:#x}", w[7], Pair(w, 8));
        BindMemory(w[2], Pair(w, 3), Pair(w, 5), "423");
        TipcReplyWords(ctx, {});
    }

    void BindMemory(u32 id, u64 address, u64 size, const char* command) {
        std::scoped_lock lk{m_objects->mutex};
        auto& memory = m_objects->memories[id];
        memory.address = address;
        if (memory.size == 0) {
            memory.size = size;
        }
        LOG_INFO(Service, "nx2:gsv {} bind native memory {} at {:#x}+{:#x}", command, id, address,
                 size);
    }

    // 429(preference[16], u32 count, u32 memory id): result, u32 native image id.
    void CreateNativeImage(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        std::scoped_lock lk{m_objects->mutex};
        const u32 id = m_objects->next_image++;
        auto& image = m_objects->images[id];
        image.memory = w[19];
        std::copy_n(w + 2, image.preference.size(), image.preference.begin());
        image.size = Nx2CalculateImageLayout(w + 2).size;
        LOG_INFO(Service, "nx2:gsv 429 CreateNativeImage memory={} count={} size={:#x} -> id {}",
                 w[19], w[18], image.size, id);
        TipcReplyWords(ctx, {id});
    }

    void DestroyNativeImage(HLERequestContext& ctx) {
        const u32 id = ctx.CommandBuffer()[2];
        std::scoped_lock lk{m_objects->mutex};
        m_objects->images.erase(id);
        TipcReplyWords(ctx, {});
    }

    // 315(u32 image, u32 count) / 314(u32 image): add/remove a window alias image pool.
    void ConfigureWindowImagePool(HLERequestContext& ctx) {
        const u32 id = ctx.CommandBuffer()[2];
        const bool create = ctx.GetCommand() == 315;
        std::scoped_lock lk{m_objects->mutex};
        if (auto it = m_objects->images.find(id); it != m_objects->images.end()) {
            it->second.window_pool = create;
        }
        LOG_INFO(Service, "nx2:gsv {} window image pool image {}", ctx.GetCommand(), id);
        TipcReplyWords(ctx, {});
    }

    void OpenViSession(HLERequestContext& ctx) {
        // Used by nn::vi2::sdk::Initialize; takes one u64, returns a session (handle at word 3,
        // result at word 4).
        const auto* cmd_buf = ctx.CommandBuffer();
        const size_t data = TipcDataOffset(cmd_buf);
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 305 arg={:#x}, returning a nx2:vi2 session",
                    u64{cmd_buf[data]} | (u64{cmd_buf[data + 1]} << 32));
        ReturnRawSession(system, ctx, std::make_shared<INx2Vi2Session>(system, m_objects));
    }

    void OpenGpuSession(HLERequestContext& ctx) {
        // Used by nn::gpu::sdk::BootWithRemoteMemory; takes one u64, returns a session (handle at
        // word 3, result at word 4) that the GPU SDK keeps.
        const auto* cmd_buf = ctx.CommandBuffer();
        const size_t data = TipcDataOffset(cmd_buf);
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 497 arg={:#x}, returning a nx2:gpu session",
                    u64{cmd_buf[data]} | (u64{cmd_buf[data + 1]} << 32));
        ReturnRawSession(system, ctx, std::make_shared<INx2GpuSession>(system));
    }

    void OpenNvsrvSession(HLERequestContext& ctx) {
        // The SDK reads the handle at word 3 and the result at word 4.
        // nvsrv speaks the nvdrv INvDrvServices protocol (Open/Ioctl/Close/...,
        // 13 = SetGraphicsFirmwareMemoryMarginEnabled), so serve it with Eden's nvdrv on the shared
        // module. The nx2: name keeps unknown commands auto-stubbed and logged.
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 1402, returning an nvdrv-backed nx2:nvsrv session");
        auto module = system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv", true)->GetModule();
        auto nvsrv = std::make_shared<Nvidia::NVDRV>(system, module, "nx2:nvsrv");
        nvsrv->InitializeForProcess(ctx.GetThread().GetOwnerProcess());
        ReturnRawSession(system, ctx, std::move(nvsrv));
    }

    static constexpr std::chrono::nanoseconds SyncptPollInterval{std::chrono::microseconds{500}};

    std::shared_ptr<Nx2NativeObjects> m_objects{std::make_shared<Nx2NativeObjects>()};
    KernelHelpers::ServiceContext m_context;
    std::array<Kernel::KEvent*, 2> m_syncpt_events{};
    Core::Memory::Memory* m_syncpt_memory{};
    u64 m_syncpt_rw{};
    u64 m_syncpt_ro{};
    u32 m_syncpt_count{};
    std::shared_ptr<Core::Timing::EventType> m_syncpt_poll;
};

} // namespace Service

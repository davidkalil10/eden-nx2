// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string_view>

#include "common/assert.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/hle_ipc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/memory.h"
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/syncpoint_manager.h"

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

// Replies to a TIPC request with success and raw u32 data words.
// TIPC has a one-word result followed directly by the data.
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

// TIPC 0(u64 token) on Switch 2 sessions: handshake echoing the token.
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

class INx2SyncptSession final : public ServiceFramework<INx2SyncptSession> {
public:
    explicit INx2SyncptSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:syncpt"}, m_context{system_, "nx2:syncpt"} {
        static const FunctionInfo functions[] = {
            {0, &INx2SyncptSession::Handshake, "Ping"},
            {1, &INx2SyncptSession::GetCount, "GetCount"},
            {22, &INx2SyncptSession::GetSlotEvent, "GetSlotEvent"},
            {103, &INx2SyncptSession::QueryCounter, "QueryCounter"},
            {201, &INx2SyncptSession::RequestSlotEvent, "RequestSlotEvent"},
            {202, &INx2SyncptSession::CancelSlotEvent, "CancelSlotEvent"},
        };
        RegisterHandlersTipc(functions);
    }

    ~INx2SyncptSession() override {
        for (auto& [slot, event] : m_events) {
            if (event != nullptr) {
                m_context.CloseEvent(event);
            }
        }
    }

private:
    static constexpr u32 HostSyncpoints = 192;

    void Handshake(HLERequestContext& ctx) {
        TipcHandshake(ctx, "nx2:syncpt");
    }

    void GetCount(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:syncpt TIPC 1, returning 32");
        TipcReplyWords(ctx, {32});
    }

    void QueryCounter(HLERequestContext& ctx) {
        const u32 syncpoint = ~ctx.CommandBuffer()[2];
        const u64 value =
            syncpoint < HostSyncpoints
                ? system.Host1x().GetSyncpointManager().GetGuestSyncpointValue(syncpoint)
                : 0;
        TipcReplyWords(ctx, {static_cast<u32>(value), static_cast<u32>(value >> 32), 0});
    }

    void GetSlotEvent(HLERequestContext& ctx) {
        const u32 slot = ctx.CommandBuffer()[2];
        auto*& event = m_events[slot];
        if (event == nullptr) {
            event = m_context.CreateEvent(std::format("nx2:syncpt:Slot{:x}", slot));
        }
        IPC::ResponseBuilder rb{ctx, 2, 1, 0};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, event->GetReadableEvent());
    }

    void RequestSlotEvent(HLERequestContext& ctx) {
        const u32 slot = ctx.CommandBuffer()[2];
        auto*& event = m_events[slot];
        if (event == nullptr) {
            event = m_context.CreateEvent(std::format("nx2:syncpt:Slot{:x}", slot));
        }
        event->Signal(system.Kernel());
        TipcReplyWords(ctx, {});
    }

    void CancelSlotEvent(HLERequestContext& ctx) {
        TipcReplyWords(ctx, {});
    }

    KernelHelpers::ServiceContext m_context;
    std::map<u32, Kernel::KEvent*> m_events;
};

class INx2TipcStubSession final : public ServiceFramework<INx2TipcStubSession> {
public:
    explicit INx2TipcStubSession(Core::System& system_, const char* name)
        : ServiceFramework{system_, name}, m_name{name} {
        static const FunctionInfo functions[] = {
            {0, &INx2TipcStubSession::Handshake, "Ping"},
        };
        RegisterHandlersTipc(functions);
    }

private:
    void Handshake(HLERequestContext& ctx) {
        TipcHandshake(ctx, m_name);
    }

    const char* m_name;
};

class INx2GraphicsSystemSession final : public ServiceFramework<INx2GraphicsSystemSession> {
public:
    explicit INx2GraphicsSystemSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:gsv"}, m_context{system_, "nx2:gsv"} {
        static const FunctionInfo functions[] = {
            {0, &INx2GraphicsSystemSession::Handshake, "Ping"},
            {4, &INx2GraphicsSystemSession::Stub, "BootWithRemoteMemory"},
            {5, &INx2GraphicsSystemSession::Stub, "Unknown5"},
            {11, &INx2GraphicsSystemSession::RegisterClientHandle, "SetCurrentProcessHandle"},
            {101, &INx2GraphicsSystemSession::Stub, "CreateContext"},
            {115, &INx2GraphicsSystemSession::QueryGrantedFeatures, "BootNativeMemoryManagement"},
            {126, &INx2GraphicsSystemSession::GetSessionFlag, "GetSessionFlag"},
            {201, &INx2GraphicsSystemSession::CheckPermission, "CreateSyncptContext"},
            {202, &INx2GraphicsSystemSession::OpenSyncptSession, "InitializeSyncptContext"},
            {204, &INx2GraphicsSystemSession::AttachSyncptMemory, "AttachSyncptMemory"},
            {301, &INx2GraphicsSystemSession::CheckPermission, "CreateDisplayContext"},
            {305, &INx2GraphicsSystemSession::OpenViSession, "InitializeDisplayContext"},
            {401, &INx2GraphicsSystemSession::CheckPermission, "CreateGpuContext"},
            {402, &INx2GraphicsSystemSession::CheckPermission, "InitializeGpuContext"},
            {403, &INx2GraphicsSystemSession::CheckPermission, "DestroyGpuContext"},
            {434, &INx2GraphicsSystemSession::AttachGpuAddressSpace, "ExistsAddressSpace"},
            {494, &INx2GraphicsSystemSession::CheckPermission, "CheckPermission494"},
            {497, &INx2GraphicsSystemSession::OpenGpuSession, "CreateGpuSessionWithRemoteMemory"},
            {601, &INx2GraphicsSystemSession::Stub, "CreateCameraContext"},
            {602, &INx2GraphicsSystemSession::OpenCameraSession, "InitializeCameraContext"},
            {801, &INx2GraphicsSystemSession::CheckPermission, "CheckPermission801"},
            {901, &INx2GraphicsSystemSession::Stub, "CreateVideoDecoderContext"},
            {902, &INx2GraphicsSystemSession::OpenVideoSession, "InitializeVideoDecoderContext"},
            {905, &INx2GraphicsSystemSession::OpenVideoSession, "InitializeVideoDecoderContext2"},
            {906, &INx2GraphicsSystemSession::OpenVideoSession, "OpenVideoDecoderSessionWithVersion"},
            {1001, &INx2GraphicsSystemSession::Stub, "CreateVideoEncoderContext"},
            {1002, &INx2GraphicsSystemSession::OpenVideoSession, "InitializeVideoEncoderContext"},
            {1101, &INx2GraphicsSystemSession::CheckPermission, "CreateGraphicsFrameContext"},
            {1102, &INx2GraphicsSystemSession::CheckPermission, "InitializeGraphicsFrameContext"},
            {1111, &INx2GraphicsSystemSession::SetFeatureVersion, "SetGraphicsFrameLengthMode"},
            {1201, &INx2GraphicsSystemSession::Stub, "CreateChatContext"},
            {1202, &INx2GraphicsSystemSession::OpenChatSession, "InitializeChatContext"},
            {1402, &INx2GraphicsSystemSession::OpenNvsrvSession,
             "CreateNvDrv2UserSessionWithRemoteMemory"},
            {1501, &INx2GraphicsSystemSession::CheckPermission, "CreateInterModuleImageContext"},
            {1502, &INx2GraphicsSystemSession::CheckPermission, "InitializeInterModuleImageContext"},
            {1503, &INx2GraphicsSystemSession::CheckPermission, "CheckPermission1503"},
            {1601, &INx2GraphicsSystemSession::CheckPermission, "CheckPermission1601"},
        };
        RegisterHandlersTipc(functions);
    }

    ~INx2GraphicsSystemSession() override {
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
    static constexpr u32 SyncptValuesOffset = 0x40;
    static constexpr u32 SyncptRoSummaryOffset = 0x1040;
    static constexpr u32 SyncptRoBitsOffset = 0x1100;
    static constexpr u32 SyncptRwSummaryOffset = 0x0;
    static constexpr u32 SyncptRwBitsOffset = 0x100;
    static constexpr u32 HostSyncpoints = 192;
    static constexpr std::chrono::nanoseconds SyncptPollInterval{std::chrono::microseconds{500}};

    static size_t TipcDataOffset(const u32* cmd_buf) {
        if ((cmd_buf[1] & 0x80000000) == 0) {
            return 2;
        }
        const u32 desc = cmd_buf[2];
        return 3 + ((desc & 1) ? 2 : 0) + ((desc >> 1) & 0xF) + ((desc >> 5) & 0xF);
    }

    void Handshake(HLERequestContext& ctx) {
        TipcHandshake(ctx, "nx2:gsv");
    }

    void Stub(HLERequestContext& ctx) {
        const auto* cmd_buf = ctx.CommandBuffer();
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC {} [2]={:#x} [3]={:#x} [4]={:#x} [5]={:#x}",
                    ctx.GetCommand(), cmd_buf[2], cmd_buf[3], cmd_buf[4], cmd_buf[5]);
        TipcReplyWords(ctx, {});
    }

    void CheckPermission(HLERequestContext& ctx) {
        TipcReplyWords(ctx, {});
    }

    void QueryGrantedFeatures(HLERequestContext& ctx) {
        const auto* w = ctx.CommandBuffer();
        LOG_INFO(Service, "nx2:gsv TIPC 115 features {:#x}", u64{w[2]} | (u64{w[3]} << 32));
        TipcReplyWords(ctx, {w[2], w[3]});
    }

    void GetSessionFlag(HLERequestContext& ctx) {
        TipcReplyWords(ctx, {0});
    }

    void AttachGpuAddressSpace(HLERequestContext& ctx) {
        const u32 id = ctx.CommandBuffer()[2];
        m_address_spaces.insert(id);
        TipcReplyWords(ctx, {});
    }

    void RegisterClientHandle(HLERequestContext& ctx) {
        if (m_client_handle_registered) {
            LOG_WARNING(Service, "nx2:gsv TIPC 11 called twice");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.PushRaw<u32>(ResultUnknown.raw);
            return;
        }
        m_client_handle_registered = true;
        TipcReplyWords(ctx, {});
    }

    void SetFeatureVersion(HLERequestContext& ctx) {
        TipcReplyWords(ctx, {});
    }

    void OpenSyncptSession(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 202, returning nx2:syncpt session");
        ReturnRawSession(system, ctx, std::make_shared<INx2SyncptSession>(system));
    }

    void AttachSyncptMemory(HLERequestContext& ctx) {
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
            memory.Write32(m_syncpt_ro + 0x00, 1);
            memory.Write32(m_syncpt_ro + 0x04, 0);
            memory.Write32(m_syncpt_ro + 0x08, max_syncpoints);
            memory.Write32(m_syncpt_ro + 0x0c, SyncptValuesOffset);
            memory.Write32(m_syncpt_ro + 0x10, SyncptRwSummaryOffset);
            memory.Write32(m_syncpt_ro + 0x14, SyncptRoSummaryOffset);
            memory.Write32(m_syncpt_ro + 0x18, SyncptRwBitsOffset);
            memory.Write32(m_syncpt_ro + 0x1c, SyncptRoBitsOffset);
            UpdateSyncptValues();
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
        rb.PushCopyObjects(ctx, *m_syncpt_events[0]);
        rb.PushCopyObjects(ctx, m_syncpt_events[1]->GetReadableEvent());
    }

    void UpdateSyncptValues() {
        if (m_syncpt_memory == nullptr) {
            return;
        }
        auto& manager = system.Host1x().GetSyncpointManager();
        for (u32 id = 0; id < std::min(m_syncpt_count, HostSyncpoints); ++id) {
            const u64 value = manager.GetGuestSyncpointValue(id);
            const u64 address = m_syncpt_ro + SyncptValuesOffset + u64{id} * 8;
            if (m_syncpt_memory->Read64(address) != value) {
                m_syncpt_memory->Write64(address, value);
            }
        }
    }

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
        if (acked && m_syncpt_events[1] != nullptr) {
            m_syncpt_events[1]->Signal(system.Kernel());
        }
    }

    void OpenViSession(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 305, returning nx2:vi2 session");
        ReturnRawSession(system, ctx, std::make_shared<INx2TipcStubSession>(system, "nx2:vi2"));
    }

    void OpenGpuSession(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 497, returning nx2:gpu session");
        ReturnRawSession(system, ctx, std::make_shared<INx2TipcStubSession>(system, "nx2:gpu"));
    }

    void OpenCameraSession(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 602, returning nx2:camera session");
        ReturnRawSession(system, ctx, std::make_shared<INx2TipcStubSession>(system, "nx2:camera"));
    }

    void OpenVideoSession(HLERequestContext& ctx) {
        const bool second = ctx.GetCommand() == 1002;
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC {}, returning video session", ctx.GetCommand());
        ReturnRawSession(
            system, ctx,
            std::make_shared<INx2TipcStubSession>(system, second ? "nx2:vdec2" : "nx2:vdec"));
    }

    void OpenChatSession(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 1202, returning nx2:chat session");
        ReturnRawSession(system, ctx, std::make_shared<INx2TipcStubSession>(system, "nx2:chat"));
    }

    void OpenNvsrvSession(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) nx2:gsv TIPC 1402, returning nvdrv-backed nx2:nvsrv session");
        if (auto nvdrv = system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv", true)) {
            auto nvsrv = std::make_shared<Nvidia::NVDRV>(system, nvdrv->GetModule(), "nx2:nvsrv");
            ReturnRawSession(system, ctx, std::move(nvsrv));
        } else {
            ReturnRawSession(system, ctx,
                             std::make_shared<INx2TipcStubSession>(system, "nx2:nvsrv"));
        }
    }

    KernelHelpers::ServiceContext m_context;
    std::set<u32> m_address_spaces;
    bool m_client_handle_registered = false;
    std::array<Kernel::KEvent*, 2> m_syncpt_events{};
    Core::Memory::Memory* m_syncpt_memory{};
    u64 m_syncpt_rw{};
    u64 m_syncpt_ro{};
    u32 m_syncpt_count{};
    std::shared_ptr<Core::Timing::EventType> m_syncpt_poll;
};

} // namespace Service

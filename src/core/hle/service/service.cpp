// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <format>
#include <mutex>
#include <set>
#include <utility>
#include "common/assert.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/ipc.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nx2_trace.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/reporter.h"

namespace Service {

/// @brief Creates a function string for logging, complete with the name (or header code, depending
/// on what's passed in) the port name, and all the cmd_buf arguments.
[[maybe_unused]] static std::string MakeFunctionString(std::string_view name, std::string_view port_name, const u32* cmd_buf) {
    // Number of params == bits 0-5 + bits 6-11
    int num_params = (cmd_buf[0] & 0x3F) + ((cmd_buf[0] >> 6) & 0x3F);
    std::string function_string = std::format("fn '{}': port={}", name, port_name);
    for (int i = 1; i <= num_params; ++i)
        function_string += std::format(", cmd_buf[{}]={:#x}", i, cmd_buf[i]);
    return function_string;
}

ServiceFrameworkBase::ServiceFrameworkBase(Core::System& system_, const char* service_name_, u32 max_sessions_, InvokerFn* handler_invoker_)
    : SessionRequestHandler(system_.Kernel(), service_name_)
    , system{system_}
    , service_name{service_name_}
    , handler_invoker{handler_invoker_}
    , max_sessions{max_sessions_}
{}

ServiceFrameworkBase::~ServiceFrameworkBase() {
    // Wait for other threads to release access before destroying
    const auto guard = ServiceFrameworkBase::LockService();
}

void ServiceFrameworkBase::RegisterHandlersBase(const FunctionInfoBase* functions, std::size_t n) {
    // Usually this array is sorted by id already, so hint to insert at the end
    handlers.reserve(handlers.size() + n);
    for (std::size_t i = 0; i < n; ++i)
        handlers.emplace_hint(handlers.cend(), functions[i].expected_header, functions[i]);
}

void ServiceFrameworkBase::RegisterHandlersBaseTipc(const FunctionInfoBase* functions, std::size_t n) {
    // Usually this array is sorted by id already, so hint to insert at the end
    handlers_tipc.reserve(handlers_tipc.size() + n);
    for (std::size_t i = 0; i < n; ++i)
        handlers_tipc.emplace_hint(handlers_tipc.cend(), functions[i].expected_header, functions[i]);
}

void ServiceFrameworkBase::ReportUnimplementedFunction(HLERequestContext& ctx,
                                                       const FunctionInfoBase* info) {
    constexpr u32 StubZeroedWords = 32;
    const bool auto_stub = Settings::values.use_auto_stub || Settings::CurrentProgramIsSwitch2() ||
                           std::string_view(service_name).starts_with("nx2:");
    auto push_zeroed_stub = [&]() {
        IPC::ResponseBuilder rb{ctx, 2 + StubZeroedWords};
        if (ctx.IsTipc()) {
            rb.PushRaw<u32>(ResultSuccess.raw);
        } else {
            rb.Push(ResultSuccess);
        }
        for (u32 i = 0; i < StubZeroedWords; ++i) {
            rb.Push<u32>(0);
        }
    };

    if (auto_stub) {
        static std::mutex reported_mutex;
        static std::set<std::pair<std::string, u32>> reported;
        std::scoped_lock lk{reported_mutex};
        if (Settings::CurrentProgramIsSwitch2()) {
            const auto* b = ctx.CommandBuffer();
            const u32 off = ctx.GetDataPayloadOffset();
            const u32 in0 = ctx.IsTipc() ? b[2]
                                         : (off + 2 < IPC::COMMAND_BUFFER_LENGTH ? b[off + 2] : 0);
            const u32 in1 = ctx.IsTipc() ? b[3]
                                         : (off + 3 < IPC::COMMAND_BUFFER_LENGTH ? b[off + 3] : 0);
            Core::Nx2Trace::RecordIpc(service_name, ctx.GetCommand(),
                                      info ? info->name : "<unknown>", ctx.IsTipc(), true, in0, in1,
                                      0, 0, 0);
        }
        if (!reported.emplace(service_name, ctx.GetCommand()).second) {
            push_zeroed_stub();
            return;
        }
    }

    auto cmd_buf = ctx.CommandBuffer();
    std::string function_name = info == nullptr ? "<unknown>" : info->name;

    std::string buf;
    buf.reserve(256);

    std::format_to(std::back_inserter(buf), "function '{}({})': port='{}' cmd_buf={{[0]={:#x}", ctx.GetCommand(), function_name, service_name, cmd_buf[0]);
    for (int i = 1; i <= 8; ++i)
        std::format_to(std::back_inserter(buf), ", [{}]={:#x}", i, cmd_buf[i]);
    buf.push_back('}');

    system.GetReporter().SaveUnimplementedFunctionReport(ctx, ctx.GetCommand(), function_name, service_name);
    UNIMPLEMENTED_MSG("Unknown / unimplemented {}", buf);
    if (auto_stub) {
        LOG_WARNING(Service, "Using auto stub fallback (32 zeroed words)!");
        push_zeroed_stub();
    }
}

void ServiceFrameworkBase::InvokeRequest(HLERequestContext& ctx) {
    auto it = handlers.find(ctx.GetCommand());
    FunctionInfoBase const* info = it == handlers.end() ? nullptr : &it->second;
    if (info == nullptr || info->handler_callback == nullptr)
        return ReportUnimplementedFunction(ctx, info);

    const bool is_sw2 = Settings::CurrentProgramIsSwitch2();
    u32 in0 = 0;
    u32 in1 = 0;
    if (is_sw2) {
        const u32 in_off = ctx.GetDataPayloadOffset();
        const auto* b = ctx.CommandBuffer();
        if (in_off + 2 < IPC::COMMAND_BUFFER_LENGTH) {
            in0 = b[in_off + 2];
        }
        if (in_off + 3 < IPC::COMMAND_BUFFER_LENGTH) {
            in1 = b[in_off + 3];
        }
    }

    LOG_TRACE(Service, "{}", MakeFunctionString(info->name, GetServiceName(), ctx.CommandBuffer()));
    handler_invoker(this, info->handler_callback, ctx);
    if (is_sw2) {
        const u32 off = ctx.GetDataPayloadOffset();
        u32 res = 0;
        u32 out0 = 0;
        u32 out1 = 0;
        if (off < IPC::COMMAND_BUFFER_LENGTH) {
            const auto* b = ctx.CommandBuffer();
            res = b[off];
            if (off + 2 < IPC::COMMAND_BUFFER_LENGTH) {
                out0 = b[off + 2];
            }
            if (off + 3 < IPC::COMMAND_BUFFER_LENGTH) {
                out1 = b[off + 3];
            }
            if (res != 0) {
                LOG_WARNING(Service, "Switch2 {}::{} returned Result={:#x}", service_name,
                            info->name, res);
            }
        }
        Core::Nx2Trace::RecordIpc(service_name, ctx.GetCommand(), info->name, false, false,
                                  in0, in1, res, out0, out1);
    }
}

void ServiceFrameworkBase::InvokeRequestTipc(HLERequestContext& ctx) {
    auto it = handlers_tipc.find(ctx.GetCommand());
    FunctionInfoBase const* info = it == handlers_tipc.end() ? nullptr : &it->second;
    if (info == nullptr || info->handler_callback == nullptr)
        return ReportUnimplementedFunction(ctx, info);

    const bool is_sw2 = Settings::CurrentProgramIsSwitch2();
    const u32 in0 = is_sw2 ? ctx.CommandBuffer()[2] : 0;
    const u32 in1 = is_sw2 ? ctx.CommandBuffer()[3] : 0;

    LOG_TRACE(Service, "{}", MakeFunctionString(info->name, GetServiceName(), ctx.CommandBuffer()));
    handler_invoker(this, info->handler_callback, ctx);
    if (is_sw2) {
        const u32 off = ctx.GetDataPayloadOffset();
        u32 res = 0;
        u32 out0 = 0;
        u32 out1 = 0;
        if (off < IPC::COMMAND_BUFFER_LENGTH) {
            const auto* b = ctx.CommandBuffer();
            res = b[off];
            if (off + 1 < IPC::COMMAND_BUFFER_LENGTH) {
                out0 = b[off + 1];
            }
            if (off + 2 < IPC::COMMAND_BUFFER_LENGTH) {
                out1 = b[off + 2];
            }
            if (res != 0) {
                LOG_WARNING(Service, "Switch2 {}::{} returned Result={:#x}", service_name,
                            info->name, res);
            }
        }
        Core::Nx2Trace::RecordIpc(service_name, ctx.GetCommand(), info->name, true, false,
                                  in0, in1, res, out0, out1);
    }
}

Result ServiceFrameworkBase::HandleSyncRequest(Kernel::KServerSession& session,
                                               HLERequestContext& ctx) {
    const auto guard = LockService();

    Result result = ResultSuccess;

    switch (ctx.GetCommandType()) {
    case IPC::CommandType::Close:
    case IPC::CommandType::TIPC_Close: {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        result = IPC::ResultSessionClosed;
        break;
    }
    case IPC::CommandType::ControlWithContext:
    case IPC::CommandType::Control: {
        system.ServiceManager().InvokeControlRequest(ctx);
        break;
    }
    case IPC::CommandType::RequestWithContext:
    case IPC::CommandType::Request: {
        InvokeRequest(ctx);
        break;
    }
    default:
        if (ctx.IsTipc()) {
            InvokeRequestTipc(ctx);
            break;
        }

        UNIMPLEMENTED_MSG("command_type={}", ctx.GetCommandType());
        break;
    }

    // If emulation was shutdown, we are closing service threads, do not write the response back to
    // memory that may be shutting down as well.
    if (system.IsPoweredOn()) {
        ctx.WriteToOutgoingCommandBuffer();
    }

    return result;
}

} // namespace Service

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cctype>
#include <cstring>
#include <format>
#include <mutex>
#include <string>

#include "common/common_types.h"
#include "common/logging.h"

namespace Core::Nx2Trace {

constexpr std::size_t kMaxIpc = 64;
constexpr std::size_t kMaxSvc = 48;

struct IpcEntry {
    u64 seq{};
    char svc[28]{};
    char fn[36]{};
    u32 cmd{};
    u32 res{};
    u32 in0{};
    u32 in1{};
    u32 out0{};
    u32 out1{};
    bool is_tipc{};
    bool unimpl{};
};

struct SvcEntry {
    u64 seq{};
    u32 id{};
    u32 res{};
    u64 a0{};
    u64 a1{};
    u64 a2{};
};

struct State {
    std::mutex mtx;
    u64 seq{0};
    u64 last_dumped_seq{0};
    std::array<IpcEntry, kMaxIpc> ipc{};
    std::size_t ipc_head{0};
    std::size_t ipc_count{0};
    std::array<SvcEntry, kMaxSvc> svc{};
    std::size_t svc_head{0};
    std::size_t svc_count{0};
};

inline State& GetState() {
    static State s;
    return s;
}

inline void CopyBounded(char* dst, std::size_t cap, const char* src) {
    if (cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    std::size_t i = 0;
    for (; i + 1 < cap && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

inline std::string FormatAsciiTag(u32 w0, u32 w1) {
    char bytes[8];
    std::memcpy(bytes, &w0, 4);
    std::memcpy(bytes + 4, &w1, 4);
    std::string out;
    bool ended = false;
    for (int i = 0; i < 8; ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c == 0) {
            ended = true;
            continue;
        }
        if (ended || c < 0x20 || c >= 0x7f) {
            return {};
        }
        out.push_back(static_cast<char>(c));
    }
    if (out.size() < 2) {
        return {};
    }
    return std::format(" \"{}\"", out);
}

inline void RecordIpc(const char* svc, u32 cmd, const char* fn, bool is_tipc, bool unimpl,
                      u32 in0, u32 in1, u32 res, u32 out0, u32 out1) {
    auto& s = GetState();
    std::scoped_lock lk{s.mtx};
    auto& e = s.ipc[s.ipc_head];
    e.seq = ++s.seq;
    CopyBounded(e.svc, sizeof(e.svc), svc);
    CopyBounded(e.fn, sizeof(e.fn), fn ? fn : "<unknown>");
    e.cmd = cmd;
    e.res = res;
    e.in0 = in0;
    e.in1 = in1;
    e.out0 = out0;
    e.out1 = out1;
    e.is_tipc = is_tipc;
    e.unimpl = unimpl;
    s.ipc_head = (s.ipc_head + 1) % kMaxIpc;
    if (s.ipc_count < kMaxIpc) {
        ++s.ipc_count;
    }
}

inline void RecordSvc(u32 id, u64 a0, u64 a1, u64 a2, u32 res) {
    auto& s = GetState();
    std::scoped_lock lk{s.mtx};
    auto& e = s.svc[s.svc_head];
    e.seq = ++s.seq;
    e.id = id;
    e.res = res;
    e.a0 = a0;
    e.a1 = a1;
    e.a2 = a2;
    s.svc_head = (s.svc_head + 1) % kMaxSvc;
    if (s.svc_count < kMaxSvc) {
        ++s.svc_count;
    }
}

inline void Dump(const char* reason) {
    std::array<IpcEntry, kMaxIpc> ipc_copy{};
    std::size_t ipc_count = 0;
    std::size_t ipc_start = 0;
    std::array<SvcEntry, kMaxSvc> svc_copy{};
    std::size_t svc_count = 0;
    std::size_t svc_start = 0;
    {
        auto& s = GetState();
        std::scoped_lock lk{s.mtx};
        if (s.last_dumped_seq != 0 && s.seq <= s.last_dumped_seq + 8) {
            return;
        }
        s.last_dumped_seq = s.seq;
        ipc_copy = s.ipc;
        ipc_count = s.ipc_count;
        ipc_start = (s.ipc_head + kMaxIpc - ipc_count) % kMaxIpc;
        svc_copy = s.svc;
        svc_count = s.svc_count;
        svc_start = (s.svc_head + kMaxSvc - svc_count) % kMaxSvc;
    }

    LOG_WARNING(Service, "=== Switch2 Trace Dump ({}): last {} IPCs ===", reason ? reason : "?",
                ipc_count);
    for (std::size_t i = 0; i < ipc_count; ++i) {
        const auto& e = ipc_copy[(ipc_start + i) % kMaxIpc];
        const std::string tag = FormatAsciiTag(e.in0, e.in1);
        LOG_WARNING(Service,
                    "  [#{:05d}] {} {}::{}({}) in=({:#x},{:#x}){} -> res={:#x} out=({:#x},{:#x}){}",
                    e.seq, e.is_tipc ? "TIPC" : "CMIF", e.svc, e.fn, e.cmd, e.in0, e.in1, tag,
                    e.res, e.out0, e.out1, e.unimpl ? " [UNIMPL]" : "");
    }

    LOG_WARNING(Service, "=== Switch2 Trace Dump ({}): last {} SVCs ===", reason ? reason : "?",
                svc_count);
    for (std::size_t i = 0; i < svc_count; ++i) {
        const auto& e = svc_copy[(svc_start + i) % kMaxSvc];
        LOG_WARNING(Kernel_SVC, "  [#{:05d}] SVC {:#04x} a0={:#x} a1={:#x} a2={:#x} -> {:#x}",
                    e.seq, e.id, e.a0, e.a1, e.a2, e.res);
    }
}

} // namespace Core::Nx2Trace

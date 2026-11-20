// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef __SWITCH2__

#include <algorithm>
#include <mutex>
#include <vector>

#define u128 libnx_u128
#define s128 libnx_s128
#include <kaihen.h>
#include <switch.h>
#undef u128
#undef s128

#include "common/assert.h"
#include "common/logging.h"
#include "dynarmic/backend/arm64/switch2_code_block.h"

namespace Dynarmic::Backend::Arm64 {

namespace Switch2JitPool {
namespace {
constexpr std::size_t kPoolSize = 96u * 1024u * 1024u;
constexpr std::size_t kPageSize = 0x1000u;

struct Slice {
    std::size_t offset;
    std::size_t size;
};

struct PoolState {
    std::mutex mutex;
    Jit jit{};
    bool open = false;
    std::vector<Slice> used;
};

PoolState& State() {
    static PoolState s;
    return s;
}

bool EnsureOpenLocked(PoolState& s) {
    if (s.open) {
        return true;
    }
    const Result rc = jitCreate(&s.jit, kPoolSize);
    if (R_FAILED(rc)) {
        LOG_CRITICAL(Common, "Switch2JitPool: jitCreate(0x{:x}) failed: 0x{:x}", kPoolSize, rc);
        return false;
    }
    s.open = true;
    LOG_INFO(Common, "Switch2JitPool: created shared {} MiB kaiHEN JIT pool (type={}, rx={}, rw={})",
             kPoolSize / (1024u * 1024u), static_cast<int>(s.jit.type), s.jit.rx_addr, s.jit.rw_addr);
    return true;
}
} // namespace

bool Alloc(std::size_t size, u8** out_rx, u8** out_rw) {
    if (!size || !out_rx || !out_rw) {
        return false;
    }
    const std::size_t aligned = (size + kPageSize - 1u) & ~(kPageSize - 1u);
    PoolState& s = State();
    std::lock_guard lock(s.mutex);
    if (!EnsureOpenLocked(s)) {
        return false;
    }

    std::size_t candidate = 0;
    for (const Slice& sl : s.used) {
        if (candidate + aligned <= sl.offset) {
            break;
        }
        candidate = std::max(candidate, sl.offset + sl.size);
    }
    if (candidate + aligned > s.jit.size) {
        LOG_CRITICAL(Common, "Switch2JitPool: out of space for 0x{:x} bytes", aligned);
        return false;
    }

    s.used.push_back(Slice{candidate, aligned});
    std::sort(s.used.begin(), s.used.end(),
              [](const Slice& a, const Slice& b) { return a.offset < b.offset; });

    *out_rx = static_cast<u8*>(s.jit.rx_addr) + candidate;
    *out_rw = static_cast<u8*>(s.jit.rw_addr) + candidate;
    return true;
}

void Free(u8* rx, std::size_t size) {
    if (!rx || !size) {
        return;
    }
    PoolState& s = State();
    std::lock_guard lock(s.mutex);
    if (!s.open) {
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(s.jit.rx_addr);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(rx);
    if (addr < base || addr >= base + s.jit.size) {
        return;
    }
    const std::size_t off = addr - base;
    std::erase_if(s.used, [off](const Slice& sl) { return sl.offset == off; });
}

void Publish(const u8* rx_start, std::size_t size) {
    if (!rx_start || !size) {
        return;
    }
    PoolState& s = State();
    std::lock_guard lock(s.mutex);
    if (!s.open) {
        return;
    }
    const uintptr_t rx_base = reinterpret_cast<uintptr_t>(s.jit.rx_addr);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(rx_start);
    if (addr < rx_base) {
        return;
    }
    const std::size_t off = addr - rx_base;
    if (off >= s.jit.size) {
        return;
    }
    const std::size_t clamped = std::min(size, s.jit.size - off);

    if (s.jit.type == JitType_SetProcessMemoryPermission && s.jit.rw_addr != s.jit.src_addr) {
        const KaihenJitRange range{static_cast<u64>(off), static_cast<u64>(clamped)};
        const Result rc = kaihenJitPublishRanges(&s.jit, &range, 1);
        if (R_FAILED(rc)) {
            LOG_ERROR(Common, "Switch2JitPool: kaihenJitPublishRanges(off=0x{:x}, size=0x{:x}) failed: 0x{:x}",
                      off, clamped, rc);
        }
    } else {
        char* rw_ptr = static_cast<char*>(s.jit.rw_addr) + off;
        char* rx_ptr = static_cast<char*>(s.jit.rx_addr) + off;
        __builtin___clear_cache(rw_ptr, rw_ptr + clamped);
        if (rx_ptr != rw_ptr) {
            __builtin___clear_cache(rx_ptr, rx_ptr + clamped);
        }
    }
}

void Shutdown() {
    PoolState& s = State();
    std::lock_guard lock(s.mutex);
    s.used.clear();
    if (s.open) {
        jitClose(&s.jit);
        s.jit = {};
        s.open = false;
    }
}
} // namespace Switch2JitPool

Switch2CodeBlock::Switch2CodeBlock(std::size_t size) : size_{size} {
    ASSERT_MSG(Switch2JitPool::Alloc(size, &rx_base_, &rw_base_),
               "Failed to allocate Switch 2 kaiHEN JIT code block ({} bytes)", size);
}

Switch2CodeBlock::~Switch2CodeBlock() {
    Switch2JitPool::Free(rx_base_, size_);
}

void Switch2CodeBlock::invalidate(u32* mem, std::size_t size) {
    Switch2JitPool::Publish(reinterpret_cast<const u8*>(mem), size);
}

void Switch2CodeBlock::invalidate_all() {
    Switch2JitPool::Publish(rx_base_, size_);
}

} // namespace Dynarmic::Backend::Arm64

#endif // __SWITCH2__

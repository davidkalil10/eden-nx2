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
constexpr std::size_t kPreferredPoolSize = 208u * 1024u * 1024u; // 4 x 48 MiB + 16 MiB headroom
constexpr std::size_t kFallbackPoolSize = 144u * 1024u * 1024u;  // 4 x 32 MiB + 16 MiB headroom
constexpr std::size_t kPageSize = 0x1000u;

struct Slice {
    std::size_t offset;
    std::size_t size;
};

struct PoolState {
    std::mutex mutex;
    Jit jit{};
    bool open = false;
    std::size_t per_core_size = 32u * 1024u * 1024u;
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
    Result rc = jitCreate(&s.jit, kPreferredPoolSize);
    if (R_SUCCEEDED(rc)) {
        s.per_core_size = 48u * 1024u * 1024u;
    } else {
        LOG_WARNING(Common, "Switch2JitPool: jitCreate(208 MiB) failed (0x{:x}), falling back to 144 MiB", rc);
        rc = jitCreate(&s.jit, kFallbackPoolSize);
        if (R_FAILED(rc)) {
            LOG_CRITICAL(Common, "Switch2JitPool: jitCreate(144 MiB) failed: 0x{:x}", rc);
            return false;
        }
        s.per_core_size = 32u * 1024u * 1024u;
    }
    s.open = true;
    LOG_INFO(Common, "Switch2JitPool: created shared {} MiB kaiHEN JIT pool ({} MiB/core, type={}, rx={}, rw={})",
             s.jit.size / (1024u * 1024u), s.per_core_size / (1024u * 1024u),
             static_cast<int>(s.jit.type), s.jit.rx_addr, s.jit.rw_addr);
    return true;
}
} // namespace

std::size_t PerCoreCodeCacheSize() {
    PoolState& s = State();
    std::lock_guard lock(s.mutex);
    EnsureOpenLocked(s);
    return s.per_core_size;
}

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

void PublishRanges(const Switch2JitRange* ranges, std::size_t count) {
    if (!ranges || count == 0) {
        return;
    }
    PoolState& s = State();
    std::lock_guard lock(s.mutex);
    if (!s.open) {
        return;
    }
    const uintptr_t rx_base = reinterpret_cast<uintptr_t>(s.jit.rx_addr);
    const bool use_ipc =
        s.jit.type == JitType_SetProcessMemoryPermission && s.jit.rw_addr != s.jit.src_addr;

    std::vector<KaihenJitRange> k_ranges;
    if (use_ipc) {
        k_ranges.reserve(count);
    }

    for (std::size_t i = 0; i < count; ++i) {
        const u8* rx_start = ranges[i].rx_start;
        const std::size_t size = ranges[i].size;
        if (!rx_start || size == 0) {
            continue;
        }
        const uintptr_t addr = reinterpret_cast<uintptr_t>(rx_start);
        if (addr < rx_base) {
            continue;
        }
        const std::size_t off = addr - rx_base;
        if (off >= s.jit.size) {
            continue;
        }
        const std::size_t clamped = std::min(size, s.jit.size - off);
        if (use_ipc) {
            k_ranges.push_back(KaihenJitRange{static_cast<u64>(off), static_cast<u64>(clamped)});
        } else {
            char* rw_ptr = static_cast<char*>(s.jit.rw_addr) + off;
            char* rx_ptr = static_cast<char*>(s.jit.rx_addr) + off;
            __builtin___clear_cache(rw_ptr, rw_ptr + clamped);
            if (rx_ptr != rw_ptr) {
                __builtin___clear_cache(rx_ptr, rx_ptr + clamped);
            }
        }
    }

    if (use_ipc && !k_ranges.empty()) {
        // Align to 4 KiB pages, sort, and coalesce overlapping/adjacent ranges so ahead-block
        // compilation + relinks publish in as few kaiHEN IPC ranges as possible.
        for (auto& r : k_ranges) {
            const u64 page_start = r.offset & ~(u64{kPageSize} - 1u);
            const u64 page_end =
                std::min<u64>((r.offset + r.size + kPageSize - 1u) & ~(u64{kPageSize} - 1u),
                              static_cast<u64>(s.jit.size));
            r.offset = page_start;
            r.size = page_end > page_start ? (page_end - page_start) : 0u;
        }
        std::sort(k_ranges.begin(), k_ranges.end(),
                  [](const KaihenJitRange& a, const KaihenJitRange& b) {
                      return a.offset < b.offset;
                  });
        std::vector<KaihenJitRange> merged;
        merged.reserve(k_ranges.size());
        for (const auto& r : k_ranges) {
            if (r.size == 0) {
                continue;
            }
            if (!merged.empty() && r.offset <= merged.back().offset + merged.back().size) {
                const u64 end =
                    std::max(merged.back().offset + merged.back().size, r.offset + r.size);
                merged.back().size = end - merged.back().offset;
            } else {
                merged.push_back(r);
            }
        }
        if (!merged.empty()) {
            const Result rc = kaihenJitPublishRanges(&s.jit, merged.data(), merged.size());
            if (R_FAILED(rc)) {
                LOG_ERROR(Common, "Switch2JitPool: kaihenJitPublishRanges(count={}) failed: 0x{:x}",
                          merged.size(), rc);
            }
        }
    }
}

void Publish(const u8* rx_start, std::size_t size) {
    const Switch2JitRange single{rx_start, size};
    PublishRanges(&single, 1);
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

void Switch2CodeBlock::unprotect() {
    ++batch_depth_;
}

void Switch2CodeBlock::protect() {
    if (batch_depth_ > 0 && --batch_depth_ > 0) {
        return;
    }
    if (!pending_.empty()) {
        Switch2JitPool::PublishRanges(pending_.data(), pending_.size());
        pending_.clear();
    }
}

void Switch2CodeBlock::invalidate(u32* mem, std::size_t size) {
    if (!mem || size == 0) {
        return;
    }
    const Switch2JitRange range{reinterpret_cast<const u8*>(mem), size};
    if (batch_depth_ > 0) {
        pending_.push_back(range);
    } else {
        Switch2JitPool::PublishRanges(&range, 1);
    }
}

void Switch2CodeBlock::invalidate_all() {
    Switch2JitPool::Publish(rx_base_, size_);
}

} // namespace Dynarmic::Backend::Arm64

#endif // __SWITCH2__

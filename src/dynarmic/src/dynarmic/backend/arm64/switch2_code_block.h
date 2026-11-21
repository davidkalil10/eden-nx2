// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/common_types.h"

namespace Dynarmic::Backend::Arm64 {

struct Switch2JitRange {
    const u8* rx_start;
    std::size_t size;
};

namespace Switch2JitPool {
bool Alloc(std::size_t size, u8** out_rx, u8** out_rw);
void Free(u8* rx, std::size_t size);
void Publish(const u8* rx_start, std::size_t size);
void PublishRanges(const Switch2JitRange* ranges, std::size_t count);
void Shutdown();
} // namespace Switch2JitPool

class Switch2CodeBlock {
public:
    explicit Switch2CodeBlock(std::size_t size);
    ~Switch2CodeBlock();

    Switch2CodeBlock(const Switch2CodeBlock&) = delete;
    Switch2CodeBlock& operator=(const Switch2CodeBlock&) = delete;

    u32* ptr() const {
        return reinterpret_cast<u32*>(rx_base_);
    }

    u32* rw_ptr() const {
        return reinterpret_cast<u32*>(rw_base_);
    }

    void protect();
    void unprotect();

    void invalidate(u32* mem, std::size_t size);
    void invalidate_all();

private:
    std::size_t size_{};
    u8* rx_base_{};
    u8* rw_base_{};
    int batch_depth_{0};
    std::vector<Switch2JitRange> pending_{};
};

} // namespace Dynarmic::Backend::Arm64

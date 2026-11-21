// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdlib>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/common_types.h"

namespace Common {

template <typename BaseAddr>
class MultiLevelPageTable final {
public:
    constexpr MultiLevelPageTable() = default;
    explicit MultiLevelPageTable(std::size_t address_space_bits, std::size_t first_level_bits,
                                 std::size_t page_bits);

    ~MultiLevelPageTable() noexcept;

    MultiLevelPageTable(const MultiLevelPageTable&) = delete;
    MultiLevelPageTable& operator=(const MultiLevelPageTable&) = delete;

    MultiLevelPageTable(MultiLevelPageTable&& other) noexcept
        : address_space_bits{std::exchange(other.address_space_bits, 0)},
          first_level_bits{std::exchange(other.first_level_bits, 0)}, page_bits{std::exchange(
                                                                          other.page_bits, 0)},
          first_level_shift{std::exchange(other.first_level_shift, 0)},
          first_level_chunk_size{std::exchange(other.first_level_chunk_size, 0)},
          first_level_map{std::move(other.first_level_map)}, base_ptr{std::exchange(other.base_ptr,
                                                                                    nullptr)} {
#ifdef __SWITCH2__
        level_index_mask = std::exchange(other.level_index_mask, 0);
#endif
    }

    MultiLevelPageTable& operator=(MultiLevelPageTable&& other) noexcept {
        address_space_bits = std::exchange(other.address_space_bits, 0);
        first_level_bits = std::exchange(other.first_level_bits, 0);
        page_bits = std::exchange(other.page_bits, 0);
        first_level_shift = std::exchange(other.first_level_shift, 0);
        first_level_chunk_size = std::exchange(other.first_level_chunk_size, 0);
        alloc_size = std::exchange(other.alloc_size, 0);
#ifdef __SWITCH2__
        for (void* level : first_level_map) {
            std::free(level);
        }
        level_index_mask = std::exchange(other.level_index_mask, 0);
#endif
        first_level_map = std::move(other.first_level_map);
        base_ptr = std::exchange(other.base_ptr, nullptr);
        return *this;
    }

    void ReserveRange(u64 start, std::size_t size);

#ifdef __SWITCH2__
    [[nodiscard]] const BaseAddr& operator[](std::size_t index) const {
        static const BaseAddr unmapped{};
        const auto* const level =
            static_cast<const BaseAddr*>(first_level_map[index >> (first_level_shift - page_bits)]);
        return level ? level[index & level_index_mask] : unmapped;
    }

    [[nodiscard]] BaseAddr& operator[](std::size_t index) {
        const std::size_t level = index >> (first_level_shift - page_bits);
        if (!first_level_map[level]) {
            AllocateLevel(level);
        }
        return static_cast<BaseAddr*>(first_level_map[level])[index & level_index_mask];
    }
#else
    [[nodiscard]] const BaseAddr& operator[](std::size_t index) const {
        return base_ptr[index];
    }

    [[nodiscard]] BaseAddr& operator[](std::size_t index) {
        return base_ptr[index];
    }
#endif

    [[nodiscard]] BaseAddr* data() {
        return base_ptr;
    }

    [[nodiscard]] const BaseAddr* data() const {
        return base_ptr;
    }

private:
    void AllocateLevel(u64 level);

    std::size_t address_space_bits{};
    std::size_t first_level_bits{};
    std::size_t page_bits{};
    std::size_t first_level_shift{};
    std::size_t first_level_chunk_size{};
    std::size_t alloc_size{};
#ifdef __SWITCH2__
    std::size_t level_index_mask{};
#endif
    std::vector<void*> first_level_map{};
    BaseAddr* base_ptr{};
};

} // namespace Common

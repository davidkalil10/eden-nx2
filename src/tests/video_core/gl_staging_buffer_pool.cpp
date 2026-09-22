// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstddef>
#include <span>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "video_core/renderer_opengl/gl_staging_buffer_pool.h"

// Callers write guest data straight into the staging span, so a refused mapping must give an
// empty one. Both StagingSpan and RequestMap are reachable without a GL context.

namespace {
constexpr size_t TEXTURE_64X64_RGBA8 = 64 * 64 * 4;

/// Puts one allocation the driver refused into @p pool, which is the state RequestMap has to
/// survive. Taken by reference because StagingBuffers declares a destructor, so it has no move
/// constructor and the buffer handles it owns cannot be copied.
void SeedRefusedAllocation(OpenGL::StagingBuffers& pool, size_t alloc_size) {
    OpenGL::StagingBuffers::StagingBufferAlloc alloc{};
    alloc.map = nullptr;
    alloc.size = alloc_size;
    alloc.sync_index = 0;
    alloc.deferred = false;
    pool.allocs.emplace_back(std::move(alloc));
}
} // Anonymous namespace

TEST_CASE("StagingBuffers: RequestMap over a refused allocation hands out nothing",
          "[video_core]") {
    // The flags are the upload pool's; nothing this test reaches uses them.
    OpenGL::StagingBuffers pool{OpenGL::UPLOAD_FLAGS.storage, OpenGL::UPLOAD_FLAGS.map};
    SeedRefusedAllocation(pool, TEXTURE_64X64_RGBA8);

    // insert_fence false, so the returned map keeps no OGLSync and its destructor makes no GL
    // call; the zero handles left in the allocation make its destructor a no-op too.
    const OpenGL::StagingBufferMap map = pool.RequestMap(1024, false, false);

    REQUIRE(map.mapped_span.data() == nullptr);
    REQUIRE(map.mapped_span.size() == 0);
    REQUIRE(map.mapped_span.empty());
}

TEST_CASE("StagingBuffers: RequestMap over a good allocation is unchanged", "[video_core]") {
    std::array<u8, 4096> allocation{};
    OpenGL::StagingBuffers pool{OpenGL::UPLOAD_FLAGS.storage, OpenGL::UPLOAD_FLAGS.map};
    SeedRefusedAllocation(pool, allocation.size());
    pool.allocs[0].map = allocation.data();

    const OpenGL::StagingBufferMap map = pool.RequestMap(1024, false, false);

    REQUIRE(map.mapped_span.data() == allocation.data());
    REQUIRE(map.mapped_span.size() == 1024);
}

TEST_CASE("StagingSpan: a mapping the driver refused carries no length", "[video_core]") {
    const std::span<u8> span =
        OpenGL::StagingSpan(nullptr, TEXTURE_64X64_RGBA8, TEXTURE_64X64_RGBA8);

    // The length matters as much as the pointer: a caller writes that many bytes.
    REQUIRE(span.data() == nullptr);
    REQUIRE(span.size() == 0);
    REQUIRE(span.empty());
}

TEST_CASE("StagingSpan: a mapping the driver gave covers exactly what was asked", "[video_core]") {
    std::array<u8, 4096> allocation{};

    SECTION("the whole allocation") {
        const std::span<u8> span =
            OpenGL::StagingSpan(allocation.data(), allocation.size(), allocation.size());
        REQUIRE(span.data() == allocation.data());
        REQUIRE(span.size() == allocation.size());
    }

    SECTION("less than the allocation, which is the usual case") {
        // RequestBuffer rounds the allocation up to a power of two, so a request almost never
        // matches the buffer it lands in and the span must follow the request, not the buffer.
        const std::span<u8> span = OpenGL::StagingSpan(allocation.data(), allocation.size(), 100);
        REQUIRE(span.data() == allocation.data());
        REQUIRE(span.size() == 100);
    }

    SECTION("nothing at all") {
        // A zero request is the one size Common::NextPow2 cannot round up, so RequestBuffer
        // clamps it to a one byte allocation and a caller asking for nothing gets nothing.
        const std::span<u8> span = OpenGL::StagingSpan(allocation.data(), 1, 0);
        REQUIRE(span.data() == allocation.data());
        REQUIRE(span.empty());
    }
}

TEST_CASE("StagingSpan: no span reaches past the allocation behind it", "[video_core]") {
    std::array<u8, 4096> allocation{};

    // Unreachable through the pool, and pinned anyway: a caller indexes this span at an offset
    // it worked out elsewhere, and nothing downstream checks a bound.
    REQUIRE(
        OpenGL::StagingSpan(allocation.data(), allocation.size(), allocation.size() + 1).empty());
    REQUIRE(OpenGL::StagingSpan(allocation.data(), 0, 1).empty());
}

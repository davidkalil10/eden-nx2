// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstddef>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_opengl/maxwell_to_gl.h"
#include "video_core/surface.h"

// A missing row sends GetFormatTuple to UNREACHABLE, which aborts, so each check reads
// FORMAT_TABLE by position before it calls GetFormatTuple.

namespace {
using OpenGL::MaxwellToGL::FORMAT_TABLE;
using OpenGL::MaxwellToGL::FormatTuple;
using OpenGL::MaxwellToGL::GetFormatTuple;
using VideoCore::Surface::MaxPixelFormat;
using VideoCore::Surface::PixelFormat;

struct CompressedRow {
    PixelFormat format;
    GLenum internal_format;
};

// Core since OpenGL 4.3; the driver decodes them where the GPU cannot.
constexpr std::array ETC2_EAC_ROWS{
    CompressedRow{PixelFormat::ETC2_RGB_UNORM, GL_COMPRESSED_RGB8_ETC2},
    CompressedRow{PixelFormat::ETC2_RGBA_UNORM, GL_COMPRESSED_RGBA8_ETC2_EAC},
    CompressedRow{PixelFormat::ETC2_RGB_PTA_UNORM, GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2},
    CompressedRow{PixelFormat::ETC2_RGB_SRGB, GL_COMPRESSED_SRGB8_ETC2},
    CompressedRow{PixelFormat::ETC2_RGBA_SRGB, GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC},
    CompressedRow{PixelFormat::ETC2_RGB_PTA_SRGB, GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2},
    CompressedRow{PixelFormat::EAC_R11_UNORM, GL_COMPRESSED_R11_EAC},
    CompressedRow{PixelFormat::EAC_R11_SNORM, GL_COMPRESSED_SIGNED_R11_EAC},
    CompressedRow{PixelFormat::EAC_R11G11_UNORM, GL_COMPRESSED_RG11_EAC},
    CompressedRow{PixelFormat::EAC_R11G11_SNORM, GL_COMPRESSED_SIGNED_RG11_EAC},
};
} // Anonymous namespace

TEST_CASE("MaxwellToGL: every ETC2 and EAC format uploads as its compressed format",
          "[video_core]") {
    for (const CompressedRow& row : ETC2_EAC_ROWS) {
        const size_t index = static_cast<size_t>(row.format);
        CAPTURE(index);
        REQUIRE(FORMAT_TABLE[index].internal_format == row.internal_format);
        REQUIRE(FORMAT_TABLE[index].format == GL_NONE);
        REQUIRE(FORMAT_TABLE[index].type == GL_NONE);
        const FormatTuple tuple = GetFormatTuple(row.format);
        REQUIRE(tuple.internal_format == row.internal_format);
        REQUIRE(tuple.format == GL_NONE);
        REQUIRE(tuple.type == GL_NONE);
    }
}

TEST_CASE("MaxwellToGL: FORMAT_TABLE holds every PixelFormat's row at its index", "[video_core]") {
    for (size_t index = 0; index < MaxPixelFormat; ++index) {
        CAPTURE(index);
        REQUIRE(FORMAT_TABLE[index].internal_format != GL_NONE);
    }
    for (size_t index = 0; index < MaxPixelFormat; ++index) {
        CAPTURE(index);
        const FormatTuple tuple = GetFormatTuple(static_cast<PixelFormat>(index));
        REQUIRE(FORMAT_TABLE[index].internal_format == tuple.internal_format);
        REQUIRE(FORMAT_TABLE[index].format == tuple.format);
        REQUIRE(FORMAT_TABLE[index].type == tuple.type);
    }
}

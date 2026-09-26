// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/object_pool.h"
#include "shader_recompiler/program_header.h"
#include "video_core/shader_dump.h"

namespace {

// PRET and EXIT under the always-true predicate; the flow analysis has no case for PRET.
constexpr u64 PRET = 0xE27000000007000FULL;
constexpr u64 EXIT = 0xE30000000007000FULL;

/// A program that is one instruction repeated, and a record of what was dumped.
class OneInstruction final : public Shader::Environment {
public:
    explicit OneInstruction(u64 insn_) : insn{insn_} {}

    u64 ReadInstruction(u32) override {
        return insn;
    }
    u32 ReadCbufValue(u32, u32) override {
        return 0;
    }
    Shader::TextureType ReadTextureType(u32) override {
        return Shader::TextureType::Color2D;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return 0;
    }
    u32 TextureBoundBuffer() const override {
        return 0;
    }
    u32 LocalMemorySize() const override {
        return 0;
    }
    u32 SharedMemorySize() const override {
        return 0;
    }
    std::array<u32, 3> WorkgroupSize() const override {
        return {1, 1, 1};
    }
    bool HasHLEMacroState() const override {
        return false;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64 pipeline_hash, u64 shader_hash) override {
        dumps.emplace_back(pipeline_hash, shader_hash);
    }

    std::vector<std::pair<u64, u64>> dumps;

private:
    u64 insn;
};

constexpr u32 FIRST{static_cast<u32>(sizeof(Shader::ProgramHeader))};

} // namespace

TEST_CASE("DumpFailedStage: the flow analysis throws on PRET, the error it reports",
          "[video_core]") {
    OneInstruction env{PRET};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> blocks;
    REQUIRE_THROWS_AS(Shader::Maxwell::Flow::CFG(env, blocks, FIRST, true),
                      Shader::NotImplementedException);
}

TEST_CASE("DumpFailedStage: a stage the analysis cannot walk is dumped without a throw",
          "[video_core]") {
    OneInstruction env{PRET};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> blocks;
    REQUIRE_NOTHROW(VideoCommon::DumpFailedStage(env, blocks, true, 0x1234, 0x5678));
    REQUIRE(env.dumps == std::vector<std::pair<u64, u64>>{{0x1234, 0x5678}});
}

TEST_CASE("DumpFailedStage: a stage the analysis can walk is dumped once", "[video_core]") {
    OneInstruction env{EXIT};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> blocks;
    REQUIRE_NOTHROW(VideoCommon::DumpFailedStage(env, blocks, false, 1, 2));
    REQUIRE(env.dumps == std::vector<std::pair<u64, u64>>{{1, 2}});
}

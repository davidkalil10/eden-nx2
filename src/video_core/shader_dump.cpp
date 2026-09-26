// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shader_recompiler/environment.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/program_header.h"
#include "video_core/shader_dump.h"

namespace VideoCommon {

void DumpFailedStage(Shader::Environment& env,
                     Shader::ObjectPool<Shader::Maxwell::Flow::Block>& flow_blocks,
                     bool exits_to_dispatcher, u64 pipeline_hash, u64 shader_hash) {
    const u32 cfg_offset{static_cast<u32>(env.StartAddress() + sizeof(Shader::ProgramHeader))};
    try {
        Shader::Maxwell::Flow::CFG cfg(env, flow_blocks, cfg_offset, exits_to_dispatcher);
    } catch (const Shader::Exception&) {
        // The analysis that failed the translation fails here again; dump what it read.
    }
    env.Dump(pipeline_hash, shader_hash);
}

} // namespace VideoCommon

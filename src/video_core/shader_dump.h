// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/object_pool.h"

namespace Shader {
class Environment;
}

namespace VideoCommon {

/// Dumps a graphics stage that failed to translate, keeping what was read when the walk of its
/// flow graph throws the same error again.
void DumpFailedStage(Shader::Environment& env,
                     Shader::ObjectPool<Shader::Maxwell::Flow::Block>& flow_blocks,
                     bool exits_to_dispatcher, u64 pipeline_hash, u64 shader_hash);

} // namespace VideoCommon

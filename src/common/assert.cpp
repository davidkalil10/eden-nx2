// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef __SWITCH2__
#include <cstdio>
#endif
#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/logging.h"
#include "common/settings.h"

#ifdef _MSC_VER
extern "C" {
__declspec(dllimport) void __stdcall DebugBreak(void);
}
#endif
void AssertFailSoftImpl() {
    if (Settings::values.use_debug_asserts) {
#ifdef __SWITCH2__
        Common::Log::DumpRecentLogTail("sdmc:/switch/eden-nx2/last_eden_log_tail.txt");
        if (std::FILE* f = std::fopen("sdmc:/switch/eden-nx2/crash.txt", "a")) {
            std::fprintf(f, "AssertFailSoftImpl triggered\n");
            std::fflush(f);
            std::fclose(f);
        }
#endif
        Common::Log::Stop();
#ifndef _MSC_VER
#   if defined(ARCHITECTURE_x86_64)
        __asm__ __volatile__("int $3");
#   elif defined(ARCHITECTURE_arm64)
        __asm__ __volatile__("brk #0");
#   else
        exit(1);
#   endif
#else // POSIX ^^^ _MSC_VER vvv
        DebugBreak();
#endif
    }
}
void AssertFatalImpl() {
#ifdef __SWITCH2__
    Common::Log::DumpRecentLogTail("sdmc:/switch/eden-nx2/last_eden_log_tail.txt");
    if (std::FILE* f = std::fopen("sdmc:/switch/eden-nx2/crash.txt", "a")) {
        std::fprintf(f, "AssertFatalImpl (UNREACHABLE/ASSERT) triggered\n");
        std::fflush(f);
        std::fclose(f);
    }
#endif
    Common::Log::Stop();
    std::abort();
}

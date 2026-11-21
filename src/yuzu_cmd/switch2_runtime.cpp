// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Isolated libnx runtime hooks for Switch 2 so <switch.h> never collides with Eden's
// global `Service` namespace or `u128` type.

#ifdef __SWITCH2__

#include <cstdint>
#include <cstdio>

#define u128 libnx_u128
#define s128 libnx_s128
#include <switch.h>
#undef u128
#undef s128

extern "C" {

extern char __start__[];

struct __pthread_t;
int __syscall_thread_detach(struct __pthread_t*) {
    // newlib's pthread_detach calls this syscall hook; libnx omits it (ENOSYS),
    // which causes every std::thread::detach() in Eden to throw std::system_error.
    return 0;
}

alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    const u64 base = reinterpret_cast<u64>(__start__);
    std::FILE* const file = std::fopen("sdmc:/switch/eden-nx2/crash.txt", "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "error_desc: 0x%x\n", ctx->error_desc);
    std::fprintf(file, "esr: 0x%x\n", ctx->esr);
    std::fprintf(file, "far: 0x%lx\n", ctx->far.x);
    std::fprintf(file, "module base: 0x%lx\n", base);
    std::fprintf(file, "pc: 0x%lx (offset 0x%lx)\n", ctx->pc.x, ctx->pc.x - base);
    std::fprintf(file, "lr: 0x%lx (offset 0x%lx)\n", ctx->lr.x, ctx->lr.x - base);
    std::fprintf(file, "sp: 0x%lx\n", ctx->sp.x);
    std::fprintf(file, "fp: 0x%lx\n", ctx->fp.x);
    for (int i = 0; i < 29; ++i) {
        std::fprintf(file, "x%d: 0x%lx\n", i, ctx->cpu_gprs[i].x);
    }
    std::fflush(file);

    u64 fp = ctx->fp.x;
    for (int depth = 0; depth < 32 && fp != 0 && (fp & 0xF) == 0; ++depth) {
        MemoryInfo info{};
        u32 page_info = 0;
        if (R_FAILED(svcQueryMemory(&info, &page_info, fp)) || !(info.perm & Perm_R)) {
            break;
        }
        const u64* const frame = reinterpret_cast<const u64*>(fp);
        std::fprintf(file, "frame %d: 0x%lx (offset 0x%lx)\n", depth, frame[1], frame[1] - base);
        if (frame[0] <= fp) {
            break;
        }
        fp = frame[0];
    }
    std::fflush(file);
    std::fclose(file);
}

} // extern "C"

#endif

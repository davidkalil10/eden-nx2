/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <mutex>

#ifdef __SWITCH2__
#include "dynarmic/backend/arm64/switch2_code_block.h"
#else
#include <oaknut/code_block.hpp>
#endif
#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/common/spin_lock.h"

namespace Dynarmic {

using Backend::Arm64::Wscratch0;
using Backend::Arm64::Wscratch1;
using namespace oaknut::util;

void EmitSpinLockLock(oaknut::CodeGenerator& code, oaknut::XReg ptr) {
    oaknut::Label start, loop;

    code.MOV(Wscratch1, 1);
    code.SEVL();
    code.l(start);
    code.WFE();
    code.l(loop);
    code.LDAXR(Wscratch0, ptr);
    code.CBNZ(Wscratch0, start);
    code.STXR(Wscratch0, Wscratch1, ptr);
    code.CBNZ(Wscratch0, loop);
}

void EmitSpinLockUnlock(oaknut::CodeGenerator& code, oaknut::XReg ptr) {
    code.STLR(WZR, ptr);
}

namespace {

struct SpinLockImpl {
    SpinLockImpl();

    void Initialize();

#ifdef __SWITCH2__
    Backend::Arm64::Switch2CodeBlock mem;
#else
    oaknut::CodeBlock mem;
#endif
    oaknut::CodeGenerator code;

    void (*lock)(volatile int*);
    void (*unlock)(volatile int*);
};

std::once_flag flag;
SpinLockImpl impl;

SpinLockImpl::SpinLockImpl()
        : mem{4096}
#ifdef __SWITCH2__
        , code{mem.rw_ptr(), mem.ptr()} {}
#else
        , code{mem.ptr(), mem.ptr()} {}
#endif

void SpinLockImpl::Initialize() {
    mem.unprotect();

    lock = code.xptr<void (*)(volatile int*)>();
    EmitSpinLockLock(code, X0);
    code.RET();

    unlock = code.xptr<void (*)(volatile int*)>();
    EmitSpinLockUnlock(code, X0);
    code.RET();

    mem.protect();
#ifdef __SWITCH2__
    mem.invalidate(mem.ptr(), static_cast<std::size_t>(code.offset()));
#else
    mem.invalidate_all();
#endif
}

}  // namespace

void SpinLock::Lock() noexcept {
    std::call_once(flag, &SpinLockImpl::Initialize, impl);
    impl.lock(&storage);
}

void SpinLock::Unlock() noexcept {
    std::call_once(flag, &SpinLockImpl::Initialize, impl);
    impl.unlock(&storage);
}

}  // namespace Dynarmic

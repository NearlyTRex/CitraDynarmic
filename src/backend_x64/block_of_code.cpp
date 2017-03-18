/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>
#include <limits>

#include <xbyak.h>

#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/jitstate.h"
#include "common/assert.h"
#include "dynarmic/callbacks.h"

namespace Dynarmic {
namespace BackendX64 {

BlockOfCode::BlockOfCode(UserCallbacks cb) : Xbyak::CodeGenerator(128 * 1024 * 1024), cb(cb), constant_pool(this, 256) {
    GenRunCode();
    GenReturnFromRunCode();
    GenMemoryAccessors();
    unwind_handler.Register(this);
    user_code_begin = getCurr<CodePtr>();
}

void BlockOfCode::ClearCache() {
    SetCodePtr(user_code_begin);
}

size_t BlockOfCode::RunCode(JitState* jit_state, CodePtr basic_block, size_t cycles_to_run) const {
    constexpr size_t max_cycles_to_run = static_cast<size_t>(std::numeric_limits<decltype(jit_state->cycles_remaining)>::max());
    ASSERT(cycles_to_run <= max_cycles_to_run);

    jit_state->cycles_remaining = cycles_to_run;
    run_code(jit_state, basic_block);
    return cycles_to_run - jit_state->cycles_remaining; // Return number of cycles actually run.
}

void BlockOfCode::ReturnFromRunCode(bool MXCSR_switch) {
    jmp(MXCSR_switch ? return_from_run_code : return_from_run_code_without_mxcsr_switch);
}

void BlockOfCode::GenRunCode() {
    align();
    run_code = getCurr<RunCodeFuncType>();

    // This serves two purposes:
    // 1. It saves all the registers we as a callee need to save.
    // 2. It aligns the stack so that the code the JIT emits can assume
    //    that the stack is appropriately aligned for CALLs.
    ABI_PushCalleeSaveRegistersAndAdjustStack(this);

    mov(r15, ABI_PARAM1);
    SwitchMxcsrOnEntry();
    jmp(ABI_PARAM2);
}

void BlockOfCode::GenReturnFromRunCode() {
    return_from_run_code = getCurr<const void*>();

    SwitchMxcsrOnExit();

    return_from_run_code_without_mxcsr_switch = getCurr<const void*>();

    ABI_PopCalleeSaveRegistersAndAdjustStack(this);
    ret();
}

void BlockOfCode::GenMemoryAccessors() {
    align();
    read_memory_8 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Read8);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    read_memory_16 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Read16);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    read_memory_32 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Read32);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    read_memory_64 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Read64);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    write_memory_8 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Write8);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    write_memory_16 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Write16);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    write_memory_32 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Write32);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();

    align();
    write_memory_64 = getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStack(this);
    CallFunction(cb.memory.Write64);
    ABI_PopCallerSaveRegistersAndAdjustStack(this);
    ret();
}

void BlockOfCode::SwitchMxcsrOnEntry() {
    stmxcsr(dword[r15 + offsetof(JitState, save_host_MXCSR)]);
    ldmxcsr(dword[r15 + offsetof(JitState, guest_MXCSR)]);
}

void BlockOfCode::SwitchMxcsrOnExit() {
    stmxcsr(dword[r15 + offsetof(JitState, guest_MXCSR)]);
    ldmxcsr(dword[r15 + offsetof(JitState, save_host_MXCSR)]);
}

Xbyak::Address BlockOfCode::MConst(u64 constant) {
    return constant_pool.GetConstant(constant);
}

void BlockOfCode::nop(size_t size) {
    switch (size) {
    case 0:
        return;
    case 1:
        db(0x90);
        return;
    case 2:
        db(0x66); db(0x90);
        return;
    case 3:
        db(0x0f); db(0x1f); db(0x00);
        return;
    case 4:
        db(0x0f); db(0x1f); db(0x40); db(0x00);
        return;
    case 5:
        db(0x0f); db(0x1f); db(0x44); db(0x00); db(0x00);
        return;
    case 6:
        db(0x66); db(0x0f); db(0x1f); db(0x44); db(0x00); db(0x00);
        return;
    case 7:
        db(0x0f); db(0x1f); db(0x80); db(0x00); db(0x00); db(0x00); db(0x00);
        return;
    case 8:
        db(0x0f); db(0x1f); db(0x84); db(0x00); db(0x00); db(0x00); db(0x00); db(0x00);
        return;
    case 9:
        db(0x66); db(0x0f); db(0x1f); db(0x84); db(0x00); db(0x00); db(0x00); db(0x00); db(0x00);
        return;
    case 10:
    default:
        db(0x66); db(0x2e); db(0x0f); db(0x1f); db(0x84); db(0x00); db(0x00); db(0x00); db(0x00); db(0x00);
        nop(size - 10);
        return;
    }
}

void* BlockOfCode::AllocateFromCodeSpace(size_t alloc_size) {
    if (size_ + alloc_size >= maxSize_) {
        throw Xbyak::Error(Xbyak::ERR_CODE_IS_TOO_BIG);
    }

    void* ret = getCurr<void*>();
    size_ += alloc_size;
    memset(ret, 0, alloc_size);
    return ret;
}

void BlockOfCode::SetCodePtr(CodePtr code_ptr) {
    // The "size" defines where top_, the insertion point, is.
    size_t required_size = reinterpret_cast<const u8*>(code_ptr) - getCode();
    setSize(required_size);
}

void BlockOfCode::EnsurePatchLocationSize(CodePtr begin, size_t size) {
    size_t current_size = getCurr<const u8*>() - reinterpret_cast<const u8*>(begin);
    ASSERT(current_size <= size);
    nop(size - current_size);
}

} // namespace BackendX64
} // namespace Dynarmic

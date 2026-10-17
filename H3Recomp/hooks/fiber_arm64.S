// Minimal AArch64 context switch for guest fibers. Bionic does not provide
// getcontext/makecontext/swapcontext, so the runtime carries its own.
//
// Frame layout (176 bytes, 16-byte aligned):
//   0   x19   8   x20  16  x21  24  x22
//   32  x23   40  x24  48  x25  56  x26
//   64  x27   72  x28  80  x29  88  x30
//   96  d8    104 d9   112 d10 120 d11
//   128 d12   136 d13  144 d14 152 d15
//   160 fpcr  168 padding
//
// h3_fiber_swap(&save_sp, new_sp) saves the current callee-saved state,
// stores the stack pointer, loads new_sp, restores, and returns into the
// other fiber (or into h3_fiber_entry for a brand new stack).

.text
.p2align 4
.global h3_fiber_swap
.type h3_fiber_swap, %function
h3_fiber_swap:
    sub sp, sp, #176
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp d8,  d9,  [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]
    mrs x9, fpcr
    str x9, [sp, #160]
    str x18, [sp, #168]

    mov x9, sp
    str x9, [x0]
    mov sp, x1

    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    ldp d8,  d9,  [sp, #96]
    ldp d10, d11, [sp, #112]
    ldp d12, d13, [sp, #128]
    ldp d14, d15, [sp, #144]
    ldr x9, [sp, #160]
    msr fpcr, x9
    ldr x18, [sp, #168]
    add sp, sp, #176
    ret
.size h3_fiber_swap, . - h3_fiber_swap

.p2align 4
.global h3_fiber_entry
.type h3_fiber_entry, %function
h3_fiber_entry:
    mov x0, x19
    bl H3FiberEntryC
    brk #0
.size h3_fiber_entry, . - h3_fiber_entry

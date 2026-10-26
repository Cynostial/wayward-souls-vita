/* ARMv7 guest nonlocal jumps. Buffer offsets follow the public Android 4.2
 * machine/setjmp.h layout. VitaSDK setjmp omits d8-d15; using it here would
 * corrupt floating-point state across Objective-C exception recovery. */
.syntax unified
.arm
.fpu neon
.text
.align 2
.global ws_setjmp
.type ws_setjmp,%function
ws_setjmp:
    mov r1,#1
    b ws_sigsetjmp
.size ws_setjmp,.-ws_setjmp
.global ws_sigsetjmp
.type ws_sigsetjmp,%function
ws_sigsetjmp:
    add r2,r0,#76
    stmia r2,{r4-r14}
    add r2,r0,#8
    vstmia r2,{d8-d15}
    vmrs r2,fpscr
    str r2,[r0,#72]
    movw r2,#0xf500
    movt r2,#0x4278
    cmp r1,#0
    orrne r2,r2,#1
    str r2,[r0]
    moveq r1,#0
    streq r1,[r0,#4]
    beq 1f
    push {r0,lr}
    bl ws_signal_mask_get
    mov r1,r0
    pop {r0,lr}
    str r1,[r0,#4]
1:  mov r0,#0
    bx lr
.size ws_sigsetjmp,.-ws_sigsetjmp
.global ws_longjmp
.global ws_siglongjmp
.type ws_longjmp,%function
.type ws_siglongjmp,%function
ws_longjmp:
ws_siglongjmp:
    ldr r2,[r0]
    bic r3,r2,#1
    movw r12,#0xf500
    movt r12,#0x4278
    cmp r3,r12
    bne ws_jump_invalid
    push {r0,r1,r2,lr}
    bl ws_signal_leave_handler
    pop {r0,r1,r2,lr}
    tst r2,#1
    beq 2f
    push {r0,r1,r2,lr}
    ldr r0,[r0,#4]
    bl ws_signal_mask_restore
    pop {r0,r1,r2,lr}
2:  ldr r2,[r0,#112]
    cmp r2,#0
    beq ws_jump_invalid
    ldr r2,[r0,#116]
    cmp r2,#0
    beq ws_jump_invalid
    add r2,r0,#8
    vldmia r2,{d8-d15}
    ldr r2,[r0,#72]
    vmsr fpscr,r2
    add r2,r0,#76
    ldmia r2,{r4-r14}
    cmp r1,#0
    moveq r1,#1
    mov r0,r1
    bx lr
.size ws_longjmp,.-ws_longjmp
.size ws_siglongjmp,.-ws_siglongjmp
.section .note.GNU-stack,"",%progbits

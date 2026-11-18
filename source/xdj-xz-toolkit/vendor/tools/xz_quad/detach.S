/* SPDX-License-Identifier: GPL-2.0-only
 * Detach a diagnostic shell from telnet without a libc or kernel-version floor.
 * Usage: detach SCRIPT PID PLAYER1_FLAGS PLAYER2_FLAGS USB_LOG
 */
.syntax unified
.arm
.global _start
_start:
    mov r5, sp
    mov r7, #2
    svc #0
    cmp r0, #0
    bgt exit_ok
    blt exit_fail
    mov r7, #66
    svc #0
    cmp r0, #0
    blt exit_fail
    ldr r0, =null_path
    mov r1, #2
    mov r2, #0
    mov r7, #5
    svc #0
    cmp r0, #0
    blt exit_fail
    mov r4, r0
    mov r1, #0
dup_loop:
    mov r0, r4
    mov r7, #63
    svc #0
    cmp r0, #0
    blt exit_fail
    add r1, r1, #1
    cmp r1, #3
    blt dup_loop
    ldr r0, =shell_path
    add r1, r5, #4
    str r0, [r1]
    ldr r2, [r5]
    add r2, r2, #1
    add r2, r1, r2, lsl #2
    mov r7, #11
    svc #0
exit_fail:
    mov r0, #1
    b exit_now
exit_ok:
    mov r0, #0
exit_now:
    mov r7, #1
    svc #0
.section .rodata
null_path: .asciz "/dev/null"
shell_path: .asciz "/bin/sh"
.section .note.GNU-stack,"",%progbits

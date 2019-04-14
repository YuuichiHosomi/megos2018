; MEG-OS Loader - Assembly part for x64
; Copyright (c) 2019 MEG-OS project, All rights reserved.
; License: BSD

[BITS 64]
[section .text]

; int check_arch(void);
    global check_arch
check_arch:
    xor eax, eax
    ret


; _Noreturn void start_kernel(moe_bootinfo_t* bootinfo, uint64_t* param);
    global start_kernel
start_kernel:
    mov rax, [rcx]
    mov cr3, rax
    jmp .skip
.skip:
    mov rax, [rdx]
    mov rsp, [rdx + 8]
    xor edx, edx
    call rax
    ud2

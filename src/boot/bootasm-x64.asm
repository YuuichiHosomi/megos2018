; MEG-OS Loader - Assembly part for x64
; Copyright (c) 2019 MEG-OS project, All rights reserved.
; License: MIT

%define IA32_MISC_ENABLE_MSR    0x000001A0
%define IA32_EFER_MSR           0xC0000080
%define IA32_MISC_XD_DISABLE    2
%define IA32_EFER_NXE           11

[BITS 64]
[section .text]


; _Noreturn void start_kernel(moe_bootinfo_t* bootinfo, uint64_t* param);
    global start_kernel
start_kernel:
    mov r9, rcx
    mov r10, rdx

    ; enable XD
    mov ecx, IA32_MISC_ENABLE_MSR
    rdmsr
    btr edx, IA32_MISC_XD_DISABLE
    jnc .xd_ok
    wrmsr
.xd_ok:
    mov ecx, IA32_EFER_MSR
    rdmsr
    bts eax, IA32_EFER_NXE
    wrmsr

    mov rax, [r9]
    mov cr3, rax
    jmp .skip
.skip:

    mov rax, [r10]
    mov rsp, [r10 + 8]
    mov rcx, r9
    xor edx, edx
    mov esi, edx
    mov rdi, rcx
    call rax
    ud2

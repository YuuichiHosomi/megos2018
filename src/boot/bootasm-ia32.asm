; MEG-OS Loader - Assembly part for i386
; Copyright (c) 2019 MEG-OS project, All rights reserved.
; License: BSD

%define LOADER_CS32 0x08
%define LOADER_CS64 0x10
%define LOADER_SS   0x18

%define IA32_EFER_MSR   0xC0000080

[BITS 32]
[section .text]


; int check_arch(void);
    global _check_arch
_check_arch:
    push ebx

    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .bad

    mov eax, 0x80000001
    cpuid
    bt edx, 29
    jnc .bad

    xor eax, eax
    jmp .end

.bad:
    or eax, byte -1
.end:
    pop ebx
    ret


; _Noreturn void start_kernel(moe_bootinfo_t* bootinfo, uint64_t* param);
    global _start_kernel
_start_kernel:
    push ebp
    mov ebp, esp

    ; setup GDT
    mov ebx, __GDT
    mov ax, (__end_GDT - __GDT - 1)
    mov [ebx + 2], ax
    mov [ebx + 4], ebx
    lgdt [ebx + 2]

    ; disable paging before enter LM
    mov eax, cr0
    btr eax, 31 ; PG
    mov cr0, eax

    ; load page table for LM
    mov ebx, [ebp + 8]
    mov eax, [ebx]
    mov cr3, eax

    ; set LME
    mov ecx, IA32_EFER_MSR
    rdmsr
    bts eax, 8 ; LME
    wrmsr

    mov eax, cr4
    bts eax, 5 ; PAE
    mov cr4, eax

    ; go long mode
    mov eax, cr0
    bts eax, 31 ; PG
    mov cr0, eax

    jmp LOADER_CS64:_next64


[BITS 64]

_next64:
    mov ecx, [rbp + 8]
    mov ebx, [rbp + 12]
    mov rax, [rbx]
    mov rsp, [rbx + 8]
    xor edx, edx
    call rax
    ud2


[section .data]
align 16
__GDT:
    dw 0, 0, 0, 0                       ; 00 NULL
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF   ; 08 32bit KERNEL TEXT FLAT
    dw 0xFFFF, 0x0000, 0x9A00, 0x00AF   ; 10 64bit KERNEL TEXT FLAT
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF   ; 18 32bit KERNEL DATA FLAT
__end_GDT:


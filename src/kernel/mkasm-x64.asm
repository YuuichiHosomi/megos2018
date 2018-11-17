; Minimal Operating Environment - Assembly part for x64
; Copyright (c) 2018 MEG-OS project, All rights reserved.
; License: BSD

%define LOADER_CS32 0x08
%define LOADER_CS64 0x10
%define LOADER_SS   0x18
%define BOOT_INFO   0x0800
%define BOOTINFO_STACK_SIZE 0x04
%define BOOTINFO_STACKS     0x08
%define BOOTINFO_CR4        0x10
%define BOOTINFO_CR3        0x18
%define BOOTINFO_IDT        0x22
%define BOOTINFO_START32    0x30
%define BOOTINFO_START64    0x38
%define BOOTINFO_GDTR       0x40
%define MSR_EFER    0xC0000080
%define IA32_APIC_BASE_MSR          0x1B
%define IA32_APIC_BASE_MSR_ENABLE   0x800

[BITS 64]
[section .text]
    extern default_int_handler
    extern moe_switch_fpu_context
    extern _irq_main
    extern irq_LV_main


; int atomic_bit_test(void *p, uintptr_t bit);
    global atomic_bit_test
atomic_bit_test:
    mov rax, rdx
    shr rax, 3
    and edx, 7
    bt [rcx+rax], edx
    sbb eax, eax
    neg eax
    ret


; int atomic_bit_test_and_set(void *p, uintptr_t bit);
    global atomic_bit_test_and_set
atomic_bit_test_and_set:
    mov rax, rdx
    shr rax, 3
    and edx, 7
    lock bts [rcx+rax], edx
    sbb eax, eax
    neg eax
    ret


; int atomic_bit_test_and_clear(void *p, uintptr_t bit);
    global atomic_bit_test_and_clear
atomic_bit_test_and_clear:
    mov rax, rdx
    shr rax, 3
    and edx, 7
    lock btr [rcx+rax], edx
    sbb eax, eax
    neg eax
    ret


; void io_set_lazy_fpu_switch();
    global io_set_lazy_fpu_switch
io_set_lazy_fpu_switch:
    mov rax, cr0
    bts eax, 3 ; TS
    mov cr0, rax
    ret


; void io_finit(void);
    global io_finit
io_finit:
    fninit
    xorps xmm0, xmm0
    movq xmm1, xmm0
    movq xmm2, xmm0
    movq xmm3, xmm0
    movq xmm4, xmm0
    movq xmm5, xmm0
    movq xmm6, xmm0
    movq xmm7, xmm0
    movq xmm8, xmm0
    movq xmm9, xmm0
    movq xmm10, xmm0
    movq xmm11, xmm0
    movq xmm12, xmm0
    movq xmm13, xmm0
    movq xmm14, xmm0
    movq xmm15, xmm0
    ret


; void io_fsave(void*);
    global io_fsave
io_fsave:
    fxsave64 [rcx]
    ret


; void io_fload(void*);
    global io_fload
io_fload:
    fxrstor64 [rcx]
    ret


; void setjmp_new_thread(jmp_buf env, uintptr_t* new_sp);
    global setjmp_new_thread
setjmp_new_thread:
    lea rax, [rel _new_thread]
    mov [rcx     ], rax
    mov [rcx+0x08], rdx
    ; mov [rcx+0x10], rdx
    ret

_new_thread:
    pop rax
    pop rcx
    jmp rax


; int _setjmp(jmp_buf env);
    global _setjmp
_setjmp:
    ; cli
    push rbp
    mov rbp, rsp

    mov rax, [rbp+ 8] ; return address
    lea rdx, [rbp+16] ; old rsp
    mov [rcx     ], rax
    mov [rcx+0x08], rdx
    mov rax, [rbp] ; old rbp
    mov [rcx+0x10], rax

    mov [rcx+0x18], rbx
    mov [rcx+0x20], rsi
    mov [rcx+0x28], rdi
    mov [rcx+0x30], r12
    mov [rcx+0x38], r13
    mov [rcx+0x40], r14
    mov [rcx+0x48], r15

    xor eax, eax
    leave
    ret


; void _longjmp(jmp_buf env, int retval);
    global _longjmp
_longjmp:
    cli
    mov eax, edx
    mov rsp, [rcx+0x08]
    mov rbp, [rcx+0x10]

    mov rbx, [rcx+0x18]
    mov rsi, [rcx+0x20]
    mov rdi, [rcx+0x28]
    mov r12, [rcx+0x30]
    mov r13, [rcx+0x38]
    mov r14, [rcx+0x40]
    mov r15, [rcx+0x48]

    mov rdx, [rcx     ]

    or eax, eax
    ; lea ecx, [rax+1]
    ; cmovz eax, ecx
    jnz .nozero
    inc eax
.nozero:
    sti
    jmp rdx


; uint64_t io_rdmsr(uint32_t addr);
    global io_rdmsr
io_rdmsr:
    rdmsr
    mov eax, eax
    shl rdx, 32
    or rax, rdx
    ret


; void io_wrmsr(uint32_t addr, uint64_t val);
    global io_wrmsr
io_wrmsr:
    mov rax, rdx
    shr rdx, 32
    wrmsr
    ret


; void io_out8(uint16_t port, uint8_t val);
    global io_out8
io_out8:
    mov al, dl
    mov edx, ecx
    out dx, al
    ret


; uint8_t io_in8(uint16_t port);
    global io_in8
io_in8:
    mov edx, ecx
    in al, dx
    ret


; void io_out32(uint16_t port, uint32_t val);
    global io_out32
io_out32:
    mov eax, edx
    mov edx, ecx
    out dx, eax
    ret


; uint32_t io_in32(uint16_t port);
    global io_in32
io_in32:
    mov edx, ecx
    in eax, dx
    ret


    global io_cli
io_cli:
    cli
    ret


; int gdt_init(void);
    global gdt_init
gdt_init:

    ; load GDTR
    lea rax, [rel __GDT]
    push rax
    mov ecx, (__end_GDT - __GDT)-1
    shl rcx, 48
    push rcx
    lgdt [rsp+6]
    add rsp, 16

    ; refresh CS and SS
    mov eax, LOADER_CS64
    mov rcx, rsp
    push byte LOADER_SS
    push rcx
    pushfq
    push rax
    call _iretq

    ret


; void idt_load(void* idt, size_t limit);
    global idt_load
idt_load:
    shl rdx, 48
    push rcx
    push rdx
    lidt [rsp+6]
    add rsp, byte 16
    ret


    global _int00, _int03, _int06, _int0D, _int0E
_int00: ; #DE
    push BYTE 0
    push BYTE 0x00
    jmp short _intXX

_int03: ; #DB
    push BYTE 0
    push BYTE 0x03
    jmp short _intXX

_int06: ; #UD
    push BYTE 0
    push BYTE 0x06
    jmp short _intXX

_int0D: ; #GP
    push BYTE 0x0D
    jmp short _intXX

_int0E: ; #PF
    push BYTE 0x0E
    ; jmp short _intXX

_intXX:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr2
    push rax
    cld

    mov rcx, rsp
    call default_int_handler

    add rsp, BYTE 8 ; CR2
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, BYTE 16 ; err/intnum
_iretq:
    iretq


    global _int07
_int07: ; #NM
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    cld
    clts

    mov ecx, 512
    call moe_switch_fpu_context
 
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    iretq


    global _irq00, _irq01, _irq02, _irq0C
_irq00:
    push rcx
    mov cl, 0x00
    jmp short _irqXX

_irq01:
    push rcx
    mov cl, 0x01
    jmp short _irqXX

_irq02:
    push rcx
    mov cl, 0x02
    jmp short _irqXX

_irq0C:
    push rcx
    mov cl, 0x0C
    jmp short _irqXX

_irqXX:
    push rax
    push rdx
    push r8
    push r9
    push r10
    push r11
    cld

    call _irq_main

    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rax
    pop rcx
    iretq

    global _irq_LV
_irq_LV:
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    cld

    call irq_LV_main

    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    iretq


; _Atomic uint32_t *mp_startup_init(uint8_t vector_sipi, size_t stack_chunk_size, uintptr_t* stacks);
    global mp_startup_init
mp_startup_init:
    push rsi
    push rdi

    movzx edi, cl
    shl edi, 12
    lea rsi, [rel _mp_rm_payload]
    mov ecx, _end_mp_rm_payload - _mp_rm_payload
    rep movsb

    mov eax, BOOT_INFO
    mov [eax + BOOTINFO_STACK_SIZE], edx
    mov [eax + BOOTINFO_STACKS], r8
    lea edx, [rax + BOOTINFO_GDTR]
    lea rsi, [rel __GDT]
    mov edi, edx
    mov ecx, (__end_GDT - __GDT)/4
    rep movsd
    mov [edx+2], edx
    mov word [edx], (__end_GDT - __GDT)-1

    mov edx, 1
    mov [rax], edx
    mov rdx, cr4
    mov [rax + BOOTINFO_CR4], edx
    mov rdx, cr3
    mov [rax + BOOTINFO_CR3], rdx
    sidt [rax + BOOTINFO_IDT]

    lea ecx, [rel _startup32]
    mov edx, LOADER_CS32
    mov [rax + BOOTINFO_START32], ecx
    mov [rax + BOOTINFO_START32 + 4], edx
    lea ecx, [rel _startup_ap]
    mov edx, LOADER_CS64
    mov [rax + BOOTINFO_START64], ecx
    mov [rax + BOOTINFO_START64 + 4], edx

    pop rdi
    pop rsi
    ret


    extern moe_startup_ap
    extern apic_set_apicid_to_cpuid
_startup_ap:
    lidt [rbx + BOOTINFO_IDT]

    ; acquire stack per cpu
    mov ebp, 1
    lock xadd [rbx], ebp
    mov eax, ebp
    imul eax, [rbx + BOOTINFO_STACK_SIZE]
    mov rcx, [rbx + BOOTINFO_STACKS]
    lea rsp, [rcx + rax]
    and rsp, byte 0xF0

    ; enable APIC
    mov ecx, IA32_APIC_BASE_MSR
    rdmsr
    or eax, IA32_APIC_BASE_MSR_ENABLE
    wrmsr
    and eax, 0xFFFFF000
    ; xor ecx, ecx
    ; mov [rax + 0x270], ecx
    mov edx, [rax + 0x20]
    shr edx, 24
    mov ecx, ebp
    call apic_set_apicid_to_cpuid

    ; then enable interrupt
    sti

    mov ecx, ebp
    call moe_startup_ap
    ud2


[BITS 32]
_startup32:

    ; enter to LM
    mov eax, cr0
    bts eax, 31
    mov cr0, eax

    jmp far [ebx + BOOTINFO_START64]


[BITS 16]
_mp_rm_payload:
    cli
    xor ax, ax
    mov ds, ax
    mov ebx, BOOT_INFO

    ; enter to PM
    mov eax, cr0
    or al, 0x01
    mov cr0, eax

    lgdt [bx + BOOTINFO_GDTR]

    mov ax, LOADER_SS
    mov ss, ax
    mov ds, ax
    mov es, ax

    mov eax, [bx + BOOTINFO_CR4]
    mov cr4, eax
    mov eax, [bx + BOOTINFO_CR3]
    mov cr3 ,eax

    mov ecx, MSR_EFER
    rdmsr
    bts eax, 8 ; LME
    bts eax, 11 ; NXE
    wrmsr

    jmp dword far [bx + BOOTINFO_START32]
_end_mp_rm_payload:


[section .data]
align 16
__GDT:
    dw 0, 0, 0, 0                       ; 00 NULL
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF   ; 08 32bit KERNEL TEXT FLAT
    dw 0xFFFF, 0x0000, 0x9A00, 0x00AF   ; 10 64bit KERNEL TEXT FLAT
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF   ; 18 32bit KERNEL DATA FLAT
    dw 0xFFFF, 0x0000, 0xFA00, 0x00AF   ; 23 64bit USER TEXT FLAT
    dw 0xFFFF, 0x0000, 0xF200, 0x00CF   ; 2B 32bit USER DATA FLAT
__end_GDT:

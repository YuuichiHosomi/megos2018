; Minimal Operating Environment - Assembly part for x64
; Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
; License: BSD

%define LOADER_CS64 0x10
%define LOADER_SS   0x18

[BITS 64]
[section .text]


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

    ; mov rdx, [rcx+0x08]
    ; push byte 0
    ; push rdx
    ; pushfq
    ; push byte LOADER_CS64
    mov rdx, [rcx     ]
    ; push rdx
    ; bts dword [rsp+0x10], 9

    or eax, eax
    ; lea ecx, [rax+1]
    ; cmovz eax, ecx
    jnz .nozero
    inc eax
.nozero:
    ; iretq
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


; int gdt_init(void);
    global gdt_init
gdt_init:

    ; load GDTR
    lea rax, [rel __GDT]
    mov [rax+2], rax
    lgdt [rax]

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
    extern default_int_handler
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
    extern moe_switch_fpu_context
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
    extern _irq_main
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


; [BITS 32]
; _hoge32:
;     ret


[section .data]
align 16
__GDT:
    dw (__end_GDT-__GDT-1), 0, 0, 0     ; 00 NULL
    dw 0, 0, 0, 0                       ; 08 RESERVED
    dw 0xFFFF, 0x0000, 0x9A00, 0x00AF   ; 10 64bit KERNEL TEXT FLAT
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF   ; 18 32bit KERNEL DATA FLAT
    dw 0xFFFF, 0x0000, 0xFA00, 0x00AF   ; 23 64bit USER TEXT FLAT
    dw 0xFFFF, 0x0000, 0xF200, 0x00CF   ; 2B 32bit USER DATA FLAT
__end_GDT:

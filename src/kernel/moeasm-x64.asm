; Minimal Operating Environment - Assembly part for x64
; Copyright (c) 2018 MEG-OS project, All rights reserved.
; License: BSD

%define LOADER_CS32 0x08
%define LOADER_CS64 0x10
%define LOADER_SS   0x18

%define BOOT_INFO           0x0800
%define BOOTINFO_MAX_CPU    0x04
%define BOOTINFO_EFER       0x08
%define BOOTINFO_STACK_SIZE 0x0C
%define BOOTINFO_STACKS     0x10
%define BOOTINFO_CR3        0x18
%define BOOTINFO_IDT        0x22
%define BOOTINFO_CR4        0x2C
%define BOOTINFO_START32    0x30
%define BOOTINFO_START64    0x38
%define BOOTINFO_MSR_MISC   0x40
%define BOOTINFO_GDTR       0x50

%define	EFLAGS_IF   0x00000200
%define IA32_APIC_BASE_MSR          0x0000001B
%define IA32_APIC_BASE_MSR_ENABLE   0x00000800
%define IA32_MISC_MSR   0x000001A0
%define IA32_EFER_MSR   0xC0000080

[BITS 64]
[section .text]
    extern default_int_handler
    extern moe_fpu_restore
    extern _irq_main
    extern ipi_sche_main


; int atomic_bit_test(void *p, uintptr_t bit);
    global atomic_bit_test
atomic_bit_test:
    bt [rcx], edx
    sbb eax, eax
    neg eax
    ret


; int atomic_bit_test_and_set(void *p, uintptr_t bit);
    global atomic_bit_test_and_set
atomic_bit_test_and_set:
    lock bts [rcx], edx
    sbb eax, eax
    neg eax
    ret


; int atomic_bit_test_and_clear(void *p, uintptr_t bit);
    global atomic_bit_test_and_clear
atomic_bit_test_and_clear:
    lock btr [rcx], edx
    sbb eax, eax
    neg eax
    ret


; void io_set_lazy_fpu_restore();
io_set_lazy_fpu_restore:
    push rax
    mov rax, cr0
    bts rax, 3 ; TS
    mov cr0, rax
    pop rax
    ret


; void io_finit(void);
    global io_finit
io_finit:
    clts
    fninit
    xorps xmm0, xmm0
    xorps xmm1, xmm1
    xorps xmm2, xmm2
    xorps xmm3, xmm3
    xorps xmm4, xmm4
    xorps xmm5, xmm5
    xorps xmm6, xmm6
    xorps xmm7, xmm7
    xorps xmm8, xmm8
    xorps xmm9, xmm9
    xorps xmm10, xmm10
    xorps xmm11, xmm11
    xorps xmm12, xmm12
    xorps xmm13, xmm13
    xorps xmm14, xmm14
    xorps xmm15, xmm15
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

    extern on_thread_start
_new_thread:
    call io_set_lazy_fpu_restore
    call on_thread_start
    sti
    pop rax
    pop rcx
    call rax
    ud2


; int _setjmp(jmp_buf env);
    global _setjmp
_setjmp:
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
    call io_set_lazy_fpu_restore
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
    jmp rdx


; uint32_t io_lock_irq(void);
    global io_lock_irq
io_lock_irq:
    pushfq
    cli
    pop rax
    and eax, EFLAGS_IF
    ret


; void io_unlock_irq(uint32_t);
    global io_unlock_irq
io_unlock_irq:
    and ecx, EFLAGS_IF
    jz .nosti
    sti
.nosti:
    ret

; void io_set_ptbr(uintptr_t cr3);
; uintptr_t io_get_ptbr();
    global io_set_ptbr, io_get_ptbr
io_set_ptbr:
    mov cr3, rcx
    jmp short .next
.next:
    ret

io_get_ptbr:
    mov rax, cr3
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


    global _int00, _int03, _int06, _int08, _int0D, _int0E
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

_int08: ; #DF
    push BYTE 0x08
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
    call moe_fpu_restore
 
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    iretq


    global _irq0, _irq1, _irq2, _irq3, _irq4, _irq5, _irq6, _irq7
    global _irq8, _irq9, _irq10, _irq11, _irq12, _irq13, _irq14, _irq15
    global _irq16, _irq17, _irq18, _irq19, _irq20, _irq21, _irq22, _irq23
    global _irq24, _irq25, _irq26, _irq27, _irq28, _irq29, _irq30, _irq31
    global _irq32, _irq33, _irq34, _irq35, _irq36, _irq37, _irq38, _irq39
    global _irq40, _irq41, _irq42, _irq43, _irq44, _irq45, _irq46, _irq47

_irq47:
	push rcx
	mov cl, 47
	jmp _irqXX
_irq46:
	push rcx
	mov cl, 46
	jmp _irqXX
_irq45:
	push rcx
	mov cl, 45
	jmp _irqXX
_irq44:
	push rcx
	mov cl, 44
	jmp _irqXX
_irq43:
	push rcx
	mov cl, 43
	jmp _irqXX
_irq42:
	push rcx
	mov cl, 42
	jmp _irqXX
_irq41:
	push rcx
	mov cl, 41
	jmp _irqXX
_irq40:
	push rcx
	mov cl, 40
	jmp _irqXX
_irq39:
	push rcx
	mov cl, 39
	jmp _irqXX
_irq38:
	push rcx
	mov cl, 38
	jmp _irqXX
_irq37:
	push rcx
	mov cl, 37
	jmp _irqXX
_irq36:
	push rcx
	mov cl, 36
	jmp _irqXX
_irq35:
	push rcx
	mov cl, 35
	jmp _irqXX
_irq34:
	push rcx
	mov cl, 34
	jmp _irqXX
_irq33:
	push rcx
	mov cl, 33
	jmp _irqXX
_irq32:
	push rcx
	mov cl, 32
	jmp _irqXX
_irq31:
	push rcx
	mov cl, 31
	jmp _irqXX
_irq30:
	push rcx
	mov cl, 30
	jmp _irqXX
_irq29:
	push rcx
	mov cl, 29
	jmp _irqXX
_irq28:
	push rcx
	mov cl, 28
	jmp _irqXX
_irq27:
	push rcx
	mov cl, 27
	jmp _irqXX
_irq26:
	push rcx
	mov cl, 26
	jmp _irqXX
_irq25:
	push rcx
	mov cl, 25
	jmp _irqXX
_irq24:
	push rcx
	mov cl, 24
	jmp _irqXX
_irq23:
	push rcx
	mov cl, 23
	jmp _irqXX
_irq22:
	push rcx
	mov cl, 22
	jmp _irqXX
_irq21:
	push rcx
	mov cl, 21
	jmp _irqXX
_irq20:
	push rcx
	mov cl, 20
	jmp _irqXX
_irq19:
	push rcx
	mov cl, 19
	jmp _irqXX
_irq18:
	push rcx
	mov cl, 18
	jmp _irqXX
_irq17:
	push rcx
	mov cl, 17
	jmp _irqXX
_irq16:
	push rcx
	mov cl, 16
	jmp _irqXX
_irq15:
	push rcx
	mov cl, 15
	jmp _irqXX
_irq14:
	push rcx
	mov cl, 14
	jmp _irqXX
_irq13:
	push rcx
	mov cl, 13
	jmp _irqXX
_irq12:
	push rcx
	mov cl, 12
	jmp _irqXX
_irq11:
	push rcx
	mov cl, 11
	jmp _irqXX
_irq10:
	push rcx
	mov cl, 10
	jmp _irqXX
_irq9:
	push rcx
	mov cl, 9
	jmp _irqXX
_irq8:
	push rcx
	mov cl, 8
	jmp _irqXX
_irq7:
	push rcx
	mov cl, 7
	jmp _irqXX
_irq6:
	push rcx
	mov cl, 6
	jmp _irqXX
_irq5:
	push rcx
	mov cl, 5
	jmp _irqXX
_irq4:
	push rcx
	mov cl, 4
	jmp _irqXX
_irq3:
	push rcx
	mov cl, 3
	jmp _irqXX
_irq2:
	push rcx
	mov cl, 2
	jmp _irqXX
_irq1:
	push rcx
	mov cl, 1
	jmp _irqXX
_irq0:
	push rcx
	mov cl, 0
;	jmp _irqXX

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


    global _ipi_sche
_ipi_sche:
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    cld

    call ipi_sche_main

    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    iretq

    global _ipi_invtlb
_ipi_invtlb:
    push rax
    mov rax, cr3
    mov cr3, rax
    pop rax
    iretq


; _Atomic uint32_t *smp_setup_init(uint8_t vector_sipi, int max_cpu, size_t stack_chunk_size, uintptr_t* stacks);
    global smp_setup_init
smp_setup_init:
    push rsi
    push rdi

    movzx edi, cl
    shl edi, 12
    lea rsi, [rel _mp_rm_payload]
    mov ecx, _end_mp_rm_payload - _mp_rm_payload
    rep movsb

    mov r10d, BOOT_INFO
    mov [r10 + BOOTINFO_MAX_CPU], edx
    mov [r10 + BOOTINFO_STACK_SIZE], r8d
    mov [r10 + BOOTINFO_STACKS], r9
    lea edx, [r10 + BOOTINFO_GDTR]
    lea rsi, [rel __GDT]
    mov edi, edx
    mov ecx, (__end_GDT - __GDT)/4
    rep movsd
    mov [edx+2], edx
    mov word [edx], (__end_GDT - __GDT)-1

    mov edx, 1
    mov [r10], edx
    mov rdx, cr4
    mov [r10 + BOOTINFO_CR4], edx
    mov rdx, cr3
    mov [r10 + BOOTINFO_CR3], rdx
    sidt [r10 + BOOTINFO_IDT]
    mov ecx, IA32_EFER_MSR
    rdmsr
    mov [r10 + BOOTINFO_EFER], eax
    mov ecx, IA32_MISC_MSR
    rdmsr
    mov [r10 + IA32_MISC_MSR], eax
    mov [r10 + IA32_MISC_MSR + 4], edx

    lea ecx, [rel _startup32]
    mov edx, LOADER_CS32
    mov [r10 + BOOTINFO_START32], ecx
    mov [r10 + BOOTINFO_START32 + 4], edx
    lea ecx, [rel _startup_ap]
    mov edx, LOADER_CS64
    mov [r10 + BOOTINFO_START64], ecx
    mov [r10 + BOOTINFO_START64 + 4], edx

    mov eax, r10d
    pop rdi
    pop rsi
    ret


    extern apic_init_ap
    extern moe_startup_ap
_startup_ap:
    lidt [rbx + BOOTINFO_IDT]

    ; init stack pointer
    mov eax, ebp
    imul eax, [rbx + BOOTINFO_STACK_SIZE]
    mov rcx, [rbx + BOOTINFO_STACKS]
    lea rsp, [rcx + rax]
    and rsp, byte 0xF0

    ; init APIC
    mov ecx, ebp
    call apic_init_ap

    ; then enable interrupt
    sti

    ; go to scheduler
    mov ecx, ebp
    call moe_startup_ap
    ud2


[BITS 16]
_mp_rm_payload:
    cli
    xor ax, ax
    mov ds, ax
    mov ebx, BOOT_INFO

    ; acquire core-id
    mov al, [bx]
    mov cl, [bx + BOOTINFO_MAX_CPU]
.loop:
    cmp al, cl
    jae .fail
    mov dl, al
    inc dx
    lock cmpxchg [bx], dl
    jz .core_ok
    pause
    jmp short .loop
.fail:
.forever:
    hlt
    jmp short .forever

.core_ok:
    movzx ebp, al

    lgdt [bx + BOOTINFO_GDTR]

    ; enter to PM
    mov eax, cr0
    or al, 0x01
    mov cr0, eax

    jmp dword far [bx + BOOTINFO_START32]
_end_mp_rm_payload:


[BITS 32]
_startup32:

    mov eax, LOADER_SS
    mov ss, eax
    mov ds, eax
    mov es, eax

    mov eax, [ebx + BOOTINFO_CR4]
    mov cr4, eax
    mov eax, [ebx + BOOTINFO_CR3]
    mov cr3 ,eax

    mov eax, [ebx + BOOTINFO_MSR_MISC]
    mov edx, [ebx + BOOTINFO_MSR_MISC + 4]
    mov ecx, IA32_MISC_MSR
    wrmsr

    mov ecx, IA32_EFER_MSR
    xor edx, edx
    mov eax, [ebx+ BOOTINFO_EFER]
    wrmsr

    ; enter to LM
    mov eax, cr0
    bts eax, 31
    mov cr0, eax

    jmp far [ebx + BOOTINFO_START64]


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

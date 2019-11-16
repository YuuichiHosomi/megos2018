;; Minimal Operating Environment - Assembly part for x64
;; Copyright (c) 2018 MEG-OS project, All rights reserved.
;; License: MIT

; %define LOADER_CS32         0x08
%define KERNEL_CS64         0x10
%define KERNEL_SS           0x18
%define USER_CS32           0x23
%define USER_CS64           0x33
%define SEL_TSS             0x40

%define SMPINFO             0x0800
%define SMPINFO_MAX_CPU     0x04
%define SMPINFO_EFER        0x08
%define SMPINFO_STACK_SIZE  0x0C
%define SMPINFO_STACK_BASE  0x10
%define SMPINFO_CR3         0x18
%define SMPINFO_IDT         0x22
%define SMPINFO_CR4         0x2C
%define SMPINFO_START64     0x30
%define SMPINFO_START_AP    0x38
%define SMPINFO_MSR_MISC    0x40
%define SMPINFO_GDTR        0x50

%define	EFLAGS_IF           0x00000200
%define EFLAGS_DF           0x00000400
%define IA32_APIC_BASE      0x0000001B
%define IA32_APIC_BASE_ENABLE   0x00000800
%define IA32_MISC           0x000001A0
%define IA32_EFER           0xC0000080
%define IA32_STAR           0xC0000081
%define IA32_LSTAR          0xC0000082
%define IA32_CSTAR          0xC0000083
%define IA32_FMASK          0xC0000084
%define IA32_FS_BASE        0xC0000100
%define IA32_GS_BASE        0xC0000101
%define IA32_KERNEL_GS_BASE 0xC0000102
%define IA32_TSC_AUX        0xC0000103

%define TSS64_RSP0          0x0004


[BITS 64]
[section .text]
    extern default_int_handler
    extern thread_lazy_fpu_restore
    extern _irq_main
    extern smp_init_ap
    extern ipi_sche_main
    extern ipi_invtlb_main
    extern thread_on_start
    extern moe_exit_thread
    extern arch_syscall_entry


    global __chkstk
__chkstk:
    ret


;; int cpu_init();
    global cpu_init
cpu_init:

    ; load GDTR
    lea rax, [rel __GDT]
    push rax
    mov ecx, (__end_GDT - __GDT)-1
    shl rcx, 48
    push rcx
    lgdt [rsp+6]
    add rsp, 16

    ; refresh CS and SS
    mov eax, KERNEL_CS64
    mov rcx, rsp
    push byte KERNEL_SS
    push rcx
    pushfq
    push rax
    call _iretq

    xor ecx, ecx
    mov ds, ecx
    mov es, ecx
    mov fs, ecx
    mov gs, ecx
    lldt cx

    mov ecx, IA32_EFER
    rdmsr
    bts eax, 0 ; SCE
    wrmsr

    call _setup_syscall

    mov eax, KERNEL_CS64
    ret


;; void idt_load(void* idt, size_t limit);
    global idt_load
idt_load:
    shl rdx, 48
    push rcx
    push rdx
    lidt [rsp+6]
    add rsp, byte 16
    ret


; size_t gdt_preferred_size();
    global gdt_preferred_size
gdt_preferred_size:
    mov eax, (__end_common_GDT - __GDT)
    ret


; void gdt_load(void *gdt, x64_tss_desc_t *tss);
    global gdt_load
gdt_load:
    push rsi
    push rdi

    mov r11, rcx

    mov rdi, rcx
    lea rsi, [rel __GDT]
    mov ecx, (__end_common_GDT - __GDT) / 8
    rep movsq

    mov rax, [rdx]
    mov rcx, [rdx + 8]
    mov [r11 + SEL_TSS], rax
    mov [r11 + SEL_TSS + 8], rcx

    push r11
    mov ecx, (__end_GDT - __GDT)-1
    shl rcx, 48
    push rcx
    lgdt [rsp+6]
    add rsp, 16

    mov ecx, SEL_TSS
    ltr cx

    pop rdi
    pop rsi
    ret


;; void *cpu_get_tss();
    global cpu_get_tss
cpu_get_tss:
    push rcx
    sub rsp, byte 0x10

    sgdt [rsp + 6]
    mov rax, [rsp + 8]
    mov rcx, dword 0xFFFFF000
    and rax, rcx

    add rsp, byte 0x10
    pop rcx
    ret


_setup_syscall:

    xor eax, eax
    mov edx, USER_CS32 << 16 | KERNEL_CS64
    mov ecx, IA32_STAR
    wrmsr

    lea rax, [rel _syscall_entry64]
    mov rdx, rax
    shr rdx, 32
    mov ecx, IA32_LSTAR
    wrmsr

    mov eax, EFLAGS_DF
    xor edx, edx
    mov ecx, IA32_FMASK
    wrmsr

    ret


_syscall_entry64:
    push rcx
    push r11
    push rsp
    mov rbp, rsp
    and rsp, byte 0xF0
    push byte 0

    movsx ecx, al
    and ecx, eax
    call arch_syscall_entry

    mov rsp, rbp
    pop rbp
    pop r11
    pop rcx
    o64 sysret


; void _do_switch_context(cpu_context_t *from, cpu_context_t *to);
%define CTX_SP  0x08
%define CTX_BP  0x10
%define CTX_BX  0x18
%define CTX_SI  0x20
%define CTX_DI  0x28
%define CTX_R12 0x30
%define CTX_R13 0x38
%define CTX_R14 0x40
%define CTX_R15 0x48
%define CTX_TSS_RSP0    0x50
%define CTX_FPU_BASE    0x80
    global _do_switch_context
_do_switch_context:
    call io_set_lazy_fpu_restore

    mov [rcx + CTX_SP], rsp
    mov [rcx + CTX_BP], rbp
    mov [rcx + CTX_BX], rbx
    mov [rcx + CTX_SI], rsi
    mov [rcx + CTX_DI], rdi
    mov [rcx + CTX_R12], r12
    mov [rcx + CTX_R13], r13
    mov [rcx + CTX_R14], r14
    mov [rcx + CTX_R15], r15

    call cpu_get_tss
    mov r11, [rax + TSS64_RSP0]
    mov r10, [rdx + CTX_TSS_RSP0]
    mov [rcx + CTX_TSS_RSP0], r11
    mov [rax + TSS64_RSP0], r10

    mov rsp, [rdx + CTX_SP]
    mov rbp, [rdx + CTX_BP]
    mov rbx, [rdx + CTX_BX]
    mov rsi, [rdx + CTX_SI]
    mov rdi, [rdx + CTX_DI]
    mov r12, [rdx + CTX_R12]
    mov r13, [rdx + CTX_R13]
    mov r14, [rdx + CTX_R14]
    mov r15, [rdx + CTX_R15]

    xor eax, eax
    xor ecx, ecx
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d

    ret


; void cpu_finit(void);
    global cpu_finit
cpu_finit:
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


; void cpu_fsave(cpu_context_t *ctx);
    global cpu_fsave
cpu_fsave:
    fxsave64 [rcx + CTX_FPU_BASE]
    ret


; void cpu_fload(cpu_context_t *ctx);
    global cpu_fload
cpu_fload:
    clts
    fxrstor64 [rcx + CTX_FPU_BASE]
    ret



; void io_setup_new_thread(cpu_context_t *context, uintptr_t* new_sp, moe_thread_start start, void *args);
    global io_setup_new_thread
io_setup_new_thread:
    lea rax, [rel _new_thread]
    sub rdx, BYTE 0x18
    mov [rdx], rax
    mov [rdx + 0x08], r8
    mov [rdx + 0x10], r9
    mov [rcx + CTX_SP], rdx
    ret

_new_thread:
    call thread_on_start
    sti
    pop rax
    pop rcx
    call rax
    mov rcx, rax
    call moe_exit_thread
    ud2


; void io_set_lazy_fpu_restore();
    global io_set_lazy_fpu_restore
io_set_lazy_fpu_restore:
    mov rax, cr0
    bts rax, 3 ; TS
    mov cr0, rax
    ret


;; uint32_t io_lock_irq(void);
    global io_lock_irq
io_lock_irq:
    pushfq
    cli
    pop rax
    and eax, EFLAGS_IF
    ret


;; void io_restore_irq(uint32_t);
    global io_restore_irq
io_restore_irq:
    and ecx, EFLAGS_IF
    jz .nosti
    sti
.nosti:
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

    call thread_lazy_fpu_restore
 
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

    align 16
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
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    cld

    mov rcx, cr3
    mov cr3, rcx
    call ipi_invtlb_main

    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    iretq


; _Atomic uint32_t *smp_setup_init(uint8_t vector_sipi, int max_cpu, size_t stack_chunk_size, uintptr_t* stack_base);
    global smp_setup_init
smp_setup_init:
    push rsi
    push rdi

    movzx r11d, cl
    shl r11d, 12
    mov edi, r11d
    lea rsi, [rel _smp_rm_payload]
    mov ecx, _end_smp_rm_payload - _smp_rm_payload
    rep movsb

    mov r10d, SMPINFO
    mov [r10 + SMPINFO_MAX_CPU], edx
    mov [r10 + SMPINFO_STACK_SIZE], r8d
    mov [r10 + SMPINFO_STACK_BASE], r9
    lea edx, [r10 + SMPINFO_GDTR]
    lea rsi, [rel __GDT]
    mov edi, edx
    mov ecx, (__end_common_GDT - __GDT)/4
    rep movsd
    mov [edx+2], edx
    mov word [edx], (__end_common_GDT - __GDT)-1

    mov edx, 1
    mov [r10], edx
    mov rdx, cr4
    mov [r10 + SMPINFO_CR4], edx
    mov rdx, cr3
    mov [r10 + SMPINFO_CR3], rdx
    sidt [r10 + SMPINFO_IDT]
    mov ecx, IA32_EFER
    rdmsr
    mov [r10 + SMPINFO_EFER], eax
    mov ecx, IA32_MISC
    rdmsr
    mov [r10 + IA32_MISC], eax
    mov [r10 + IA32_MISC + 4], edx

    lea ecx, [r11 + _startup64 - _smp_rm_payload]
    mov edx, KERNEL_CS64
    mov [r10 + SMPINFO_START64], ecx
    mov [r10 + SMPINFO_START64 + 4], edx
    lea rax, [rel _startup_ap]
    mov [r10 + SMPINFO_START_AP], rax

    mov eax, r10d
    pop rdi
    pop rsi
    ret


_startup_ap:
    lidt [rbx + SMPINFO_IDT]

    ; init stack pointer
    mov eax, ebp
    imul eax, [rbx + SMPINFO_STACK_SIZE]
    mov rcx, [rbx + SMPINFO_STACK_BASE]
    lea rsp, [rcx + rax]

    call _setup_syscall

    ; init APIC
    mov ecx, ebp
    call smp_init_ap

    ; idle thread
    sti
.loop:
    hlt
    jmp .loop


[bits 16]

; Payload to initialize SMP Applicaition Processors
; NOTE: These sequences can't use the stack
_smp_rm_payload:
    cli
    xor ax, ax
    mov ds, ax
    mov ebx, SMPINFO

    ; acquire core-id
    mov al, [bx]
    mov cl, [bx + SMPINFO_MAX_CPU]
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

    lgdt [bx + SMPINFO_GDTR]

    ; enter to PM
    mov eax, cr0
    bts eax, 0
    mov cr0, eax

    mov ax, KERNEL_SS
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; restore BSP's system registers
    mov eax, [bx + SMPINFO_CR4]
    mov cr4, eax
    mov eax, [bx + SMPINFO_CR3]
    mov cr3 ,eax

    mov eax, [bx + SMPINFO_MSR_MISC]
    mov edx, [bx + SMPINFO_MSR_MISC + 4]
    mov ecx, IA32_MISC
    wrmsr

    mov ecx, IA32_EFER
    xor edx, edx
    mov eax, [bx+ SMPINFO_EFER]
    wrmsr

    ; enter to LM
    mov eax, cr0
    bts eax, 31
    mov cr0, eax

    ; o32 jmp far [bx + SMPINFO_START64]
    jmp far dword [bx + SMPINFO_START64]

[BITS 64]

_startup64:
    jmp [rbx + SMPINFO_START_AP]

_end_smp_rm_payload:


; void exp_user_mode(void *base, void *stack_top);
    global exp_user_mode
exp_user_mode:
    mov rbp, rsp
    mov r15, rcx
    mov r14, rdx

    mov rdi, rcx
    lea rsi, [rel _user_mode_exp_payload]
    mov ecx, _end_user_mode_exp_payload - _user_mode_exp_payload
    rep movsb

    call cpu_get_tss
    mov [rax + TSS64_RSP0], rbp

    push byte USER_CS64 + 8
    push r14
    mov eax, EFLAGS_IF
    push rax
    push byte USER_CS64
    push r15
    iretq


_user_mode_exp_payload:

    lea rsi, [rel _hello]
.loop:
    mov dl, [rsi]
    or dl, dl
    jz .end
    mov eax, 126
    syscall
    inc rsi
    jmp .loop
.end:

    mov rax, 0x123456789abcdef
    movq xmm0, rax
    movq xmm1, rsp

    mov eax, 1
    xor edx, edx
    syscall
    int3

_hello: db "Hello world!", 13, 10, 0

_end_user_mode_exp_payload:


[section .data]
align 16
__GDT:
    dw 0, 0, 0, 0                       ; 00 NULL
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF   ; 08 DPL0 CODE32 FLAT HISTORICAL
    dw 0xFFFF, 0x0000, 0x9A00, 0x00AF   ; 10 DPL0 CODE64 FLAT
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF   ; 18 DPL0 DATA FLAT MANDATORY
    dw 0xFFFF, 0x0000, 0xFA00, 0x00CF   ; 23 DPL3 CODE32 FLAT MANDATORY
    dw 0xFFFF, 0x0000, 0xF200, 0x00CF   ; 2B DPL3 DATA FLAT MANDATORY
    dw 0xFFFF, 0x0000, 0xFA00, 0x00AF   ; 33 DPL3 CODE64 FLAT
    dw 0xFFFF, 0x0000, 0xF200, 0x00CF   ; 3B DPL3 DATA FLAT MANDATORY
__end_common_GDT:
    dw 0, 0, 0, 0, 0, 0, 0, 0           ; 40:48 TSS64

__end_GDT:

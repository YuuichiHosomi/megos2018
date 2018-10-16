;
; MEG-OS MOE ASM part
;
; Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in all
; copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE.
;

%define LOADER_CS64 0x10
%define LOADER_SS   0x18

[BITS 64]
[section .text]


; uintptr_t atomic_exchange_add(volatile uintptr_t*, uintptr_t);
    global atomic_exchange_add
atomic_exchange_add:
    mov rax, rdx
    lock xadd [rcx], rax
    ret


; int atomic_compare_and_swap(volatile uintptr_t* p, uintptr_t expected, uintptr_t new_value);
    global atomic_compare_and_swap
atomic_compare_and_swap:
    mov rax, rdx
    lock cmpxchg [rcx], r8
    setz al
    movzx eax, al
    ret


; void io_pause();
    global io_pause
io_pause:
    pause
    ret


; void io_hlt();
    global io_hlt
io_hlt:
    hlt
    ret


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
    cld
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
    cld
    push rax
    push rdx
    push r8
    push r9
    push r10
    push r11

    mov rdx, rsp
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

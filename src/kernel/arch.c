/*

    Minimal Architecture Specific Initialization

    Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/
#include "moe.h"
#include "x86.h"


extern uint16_t gdtr_init(void);
extern void idtr_load(void*, size_t);
extern void* _int00;
extern void* _int03;
extern void* _int06;
extern void* _int0D;
extern void* _int0E;


/*********************************************************************/

x64_idt64_t* idt;


/*********************************************************************/

void idtr_set_handler(x64_idt64_t* idt, int num, uintptr_t offset, uint16_t sel, uint8_t ist) {
    x64_idt64_t desc;
    desc.offset_1 = offset;
    desc.sel = sel;
    desc.attr = 0x8E00 | ist;
    desc.offset_2 = offset >> 16;
    desc.u64[1] = offset >> 32;
    idt[num] = desc;
}

#define SET_SYSTEM_INT_HANDLER(num)  idtr_set_handler(idt, 0x ## num, (uintptr_t)&_int ## num, cs_sel, 0)

void idtr_init(uint16_t cs_sel) {

    const size_t idt_limit = 0x20 * sizeof(x64_idt64_t);
    idt = mm_alloc_object(idt_limit);
    memset(idt, 0, idt_limit);

    SET_SYSTEM_INT_HANDLER(00); // #DE
    SET_SYSTEM_INT_HANDLER(03); // #DB
    SET_SYSTEM_INT_HANDLER(06); // #UD
    SET_SYSTEM_INT_HANDLER(0D); // #GP
    SET_SYSTEM_INT_HANDLER(0E); // #PF

    idtr_load(idt, idt_limit-1);
}

void _intXX_handler(x64_context_t* regs) {
    printf("#### EXCEPTION %02zx-%04zx-%p\n", regs->intnum, regs->err, regs->cr2);
    printf("CS:RIP %04zx:%p SS:RSP %04zx:%p\n", regs->cs, regs->rip, regs->ss, regs->rsp);
    printf("ABCD %016zx %016zx %016zx %016zx\n", regs->rax, regs->rbx, regs->rcx, regs->rdx);
    printf("BPSD %016zx %016zx %016zx\n", regs->rbp, regs->rsi, regs->rdi);
    printf("R8-  %016zx %016zx %016zx %016zx\n", regs->r8, regs->r9, regs->r10, regs->r11);
    printf("R12- %016zx %016zx %016zx %016zx\n", regs->r12, regs->r13, regs->r14, regs->r15);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/*********************************************************************/

void arch_init() {
    uint16_t cs_sel = gdtr_init();
    idtr_init(cs_sel);
}

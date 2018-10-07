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



extern uint16_t gdt_init(void);
extern void idt_load(volatile void*, size_t);
extern void* _int00;
extern void* _int03;
extern void* _int06;
extern void* _int0D;
extern void* _int0E;
uint64_t rdmsr(uint32_t addr);
void wrmsr(uint32_t addr, uint64_t val);


/*********************************************************************/
//  IDT

x64_idt64_t* idt;

void _intXX_handler(x64_context_t* regs) {
    printf("#### EXCEPTION %02llx-%04llx-%016llx\n", regs->intnum, regs->err, regs->cr2);
    printf("CS:RIP %04llx:%016llx SS:RSP %04llx:%016llx\n", regs->cs, regs->rip, regs->ss, regs->rsp);
    printf(
        "ABCD %016llx %016llx %016llx %016llx\n"
        "BPSD %016llx %016llx %016llx\n"
        "R8-  %016llx %016llx %016llx %016llx\n"
        "R12- %016llx %016llx %016llx %016llx\n",
        regs->rax, regs->rbx, regs->rcx, regs->rdx, regs->rbp, regs->rsi, regs->rdi,
        regs->r8, regs->r9, regs->r10, regs->r11, regs->r12, regs->r13, regs->r14, regs->r15);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void idt_set_handler(uint8_t num, uintptr_t offset, uint16_t sel, uint8_t ist) {
    x64_idt64_t desc;
    desc.offset_1   = offset;
    desc.sel        = sel;
    desc.ist        = ist;
    desc.attr       = 0x8E;
    desc.offset_2   = offset >> 16;
    desc.offset_3   = offset >> 32;
    idt[num] = desc;
}

#define SET_SYSTEM_INT_HANDLER(num)  idt_set_handler(0x ## num, (uintptr_t)&_int ## num, cs_sel, 0)

void idt_init(uint16_t cs_sel) {

    const size_t idt_size = 0x80 * sizeof(x64_idt64_t);
    idt = mm_alloc_static(idt_size);
    memset((void*)idt, 0, idt_size);

    SET_SYSTEM_INT_HANDLER(00); // #DE
    SET_SYSTEM_INT_HANDLER(03); // #DB
    SET_SYSTEM_INT_HANDLER(06); // #UD
    SET_SYSTEM_INT_HANDLER(0D); // #GP
    SET_SYSTEM_INT_HANDLER(0E); // #PF

    idt_load(idt, idt_size-1);
}


/*********************************************************************/
//  Paging

void page_init() {
    ;
}

void* PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(MOE_PHYSICAL_ADDRESS pa) {
    // TODO:
    return (void*)(pa);
}

uint8_t READ_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p) {
    volatile uint8_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    return *p;
}

uint32_t READ_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p) {
    volatile uint32_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    return *p;
}

void WRITE_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p, uint32_t v) {
    volatile uint32_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    *p = v;
}

uint64_t READ_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p) {
    volatile uint64_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    return *p;
}

void WRITE_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p, uint64_t v) {
    volatile uint64_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    *p = v;
}


/*********************************************************************/
//  Advanced Programmable Interrupt Controller

MOE_PHYSICAL_ADDRESS local_apic = 0;
MOE_PHYSICAL_ADDRESS io_apic   = 0;

void apic_init() {
    acpi_madt_t* madt = acpi_find_table(ACPI_MADT_SIGNATURE);
    if (madt) {

        size_t max_length = madt->Header.length - 44;
        uint8_t* p = madt->Structure;

        local_apic = madt->lapicaddr;

        //  Disable Legacy PIC
        if (madt->Flags & ACPI_MADT_PCAT_COMPAT) {
            __asm__ volatile (
                "outb %%al, $0x21\n"
                "outb %%al, $0xA1\n"
                ::"a"(0xFF));
        }

        for (size_t loc = 0; loc < max_length; ) {
            size_t len = p[loc+1];
            switch (p[loc]) {
            case 0x01: // IOAPIC
                io_apic = *((uint32_t*)(p+loc+0x04));
                break;
            default:
                break;
            }
            loc += len;
        }

    }
}


/*********************************************************************/
//  High Precision Event Timer

MOE_PHYSICAL_ADDRESS hpet_base = 0;

void hpet_init() {
    acpi_hpet_t* hpet = acpi_find_table(ACPI_HPET_SIGNATURE);
    if (hpet) {
        hpet_base = hpet->address.address;
        WRITE_PHYSICAL_UINT64(hpet_base + 0xF0, 0); // count
        WRITE_PHYSICAL_UINT64(hpet_base + 0x10, 0x01); // ENABLE_CNF
    }
}


/*********************************************************************/

void arch_init() {
    page_init();
    uint16_t cs_sel = gdt_init();
    idt_init(cs_sel);
    apic_init();
    hpet_init();
}

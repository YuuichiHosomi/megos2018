// Architecture Specific
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "x86.h"


typedef union {
    uint64_t u64;
    struct {
        uint32_t eax, edx;
    };
} tuple_eax_edx_t;

extern void *_int00;
extern void *_int03;
extern void *_int06;
extern void *_int07;
extern void *_int08;
extern void *_int0D;
extern void *_int0E;
extern void *_ipi_sche;
extern void *_ipi_invtlb;

extern void *_irq0;
extern void *_irq1;
extern void *_irq2;
extern void *_irq3;
extern void *_irq4;
extern void *_irq5;
extern void *_irq6;
extern void *_irq7;
extern void *_irq8;
extern void *_irq9;
extern void *_irq10;
extern void *_irq11;
extern void *_irq12;
extern void *_irq13;
extern void *_irq14;
extern void *_irq15;
extern void *_irq16;
extern void *_irq17;
extern void *_irq18;
extern void *_irq19;
extern void *_irq20;
extern void *_irq21;
extern void *_irq22;
extern void *_irq23;
extern void *_irq24;
extern void *_irq25;
extern void *_irq26;
extern void *_irq27;
extern void *_irq28;
extern void *_irq29;
extern void *_irq30;
extern void *_irq31;
extern void *_irq32;
extern void *_irq33;
extern void *_irq34;
extern void *_irq35;
extern void *_irq36;
extern void *_irq37;
extern void *_irq38;
extern void *_irq39;
extern void *_irq40;
extern void *_irq41;
extern void *_irq42;
extern void *_irq43;
extern void *_irq44;
extern void *_irq45;
extern void *_irq46;
extern void *_irq47;


extern uint16_t gdt_init(x64_tss_desc_t *tss);
extern void idt_load(volatile void*, size_t);
extern x64_tss_t *io_get_tss();
extern _Atomic uint32_t *smp_setup_init(uint8_t vector_sipi, int max_cpu, size_t stack_chunk_size, uintptr_t* stacks);


static tuple_eax_edx_t io_rdmsr(uint32_t const addr) {
    tuple_eax_edx_t result;
    __asm__ volatile ("rdmsr": "=a"(result.eax), "=d"(result.edx) : "c"(addr));
    return result;
}

static void io_wrmsr(uint32_t const addr, tuple_eax_edx_t val) {
    __asm__ volatile ("wrmsr": : "c"(addr), "d"(val.edx), "a"(val.eax));
}


/*********************************************************************/
//  IDT - Interrupt Descriptor Table

#define MAX_IDT_NUM 0x100
uint16_t cs_sel;
x64_idt64_t* idt;


void idt_set_handler(uint8_t num, uintptr_t offset, uint16_t sel, uint8_t ist) {
    x64_idt64_t desc = {{0}};
    desc.offset_1   = offset;
    desc.sel        = sel;
    desc.ist        = ist;
    desc.attr       = 0x8E;
    desc.offset_2   = offset >> 16;
    desc.offset_3   = offset >> 32;
    idt[num] = desc;
}

#define SET_SYSTEM_INT_HANDLER(num, ist)  idt_set_kernel_handler(0x ## num, (uintptr_t)&_int ## num, ist)
static void idt_set_kernel_handler(uint8_t num, uintptr_t offset, uint8_t ist) {
    idt_set_handler(num, offset, cs_sel, ist);
}

void tss_init(x64_tss_desc_t *tss, uint64_t base, size_t size) {
    x64_tss_desc_t _tss = {{size - 1}};
    _tss.base_1 = base;
    _tss.base_2 = base >> 24;
    _tss.base_3 = base >> 32;
    _tss.type = DESC_TYPE_TSS64;
    _tss.present = 1;
    *tss = _tss;
}


#define BSOD_BUFF_SIZE 1024
static char bsod_buff[BSOD_BUFF_SIZE];
void default_int_handler(x64_context_t* regs) {

    snprintf(bsod_buff, BSOD_BUFF_SIZE,
        "#### EXCEPTION %02llx-%04llx-%016llx\n"
        // "Thread %d: %s\n"
        "IP %02llx:%012llx SP %02llx:%012llx F %08llx\n"
        "AX %016llx CX %016llx DX %016llx\n"
        "BX %016llx BP %016llx SI %016llx\n"
        "DI %016llx R8 %016llx R9 %016llx\n"
        "R10- %016llx %016llx %016llx\n"
        "R13- %016llx %016llx %016llx\n"
        , regs->intnum, regs->err, regs->cr2
        // , moe_get_current_thread(), moe_get_current_thread_name()
        , regs->cs, regs->rip, regs->ss, regs->rsp, regs->rflags
        , regs->rax, regs->rcx, regs->rdx, regs->rbx, regs->rbp, regs->rsi, regs->rdi
        , regs->r8, regs->r9, regs->r10, regs->r11, regs->r12, regs->r13, regs->r14, regs->r15
        );

    moe_bsod(bsod_buff);
    for (;;) { io_hlt(); }
}


void idt_init() {

    const size_t idt_size = MAX_IDT_NUM * sizeof(x64_idt64_t);
    idt = moe_alloc_object(idt_size, 1);

    // x64_tss_t *tss = io_get_tss();
    // const size_t ist_size = 0x4000;
    // for (int i = 0; i < 7; i++) {
    //     uintptr_t ist = (uintptr_t)moe_alloc_object(ist_size, 1) + ist_size;
    //     tss->IST[i] = ist;
    // }

    SET_SYSTEM_INT_HANDLER(00, 0); // #DE Divide by zero Error
    SET_SYSTEM_INT_HANDLER(03, 0); // #BP Breakpoint
    SET_SYSTEM_INT_HANDLER(06, 0); // #UD Undefined Opcode
    SET_SYSTEM_INT_HANDLER(07, 0); // #NM Device not Available
    SET_SYSTEM_INT_HANDLER(08, 0); // #DF Double Fault
    SET_SYSTEM_INT_HANDLER(0D, 0); // #GP General Protection Fault
    SET_SYSTEM_INT_HANDLER(0E, 0); // #PF Page Fault

    idt_load(idt, idt_size - 1);
}


/*********************************************************************/


void _irq_main(uint8_t irq, void* p) {
    // TODO:
}
void _int07_main() {
    // TODO:
}


/*********************************************************************/


void arch_init() {

    size_t tss_size = 4096;
    uintptr_t tss_base = (uintptr_t)moe_alloc_object(tss_size, 1);
    x64_tss_desc_t tss;
    tss_init(&tss, tss_base, tss_size);

    cs_sel = gdt_init(&tss);
    idt_init();

}

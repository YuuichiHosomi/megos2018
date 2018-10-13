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
extern void* _irq00;
extern void* _irq01;
extern void* _irq02;
uint64_t io_rdmsr(uint32_t addr);
void io_wrmsr(uint32_t addr, uint64_t val);

void io_out8(uint16_t port, uint8_t val);
uint8_t io_in8(uint16_t port);


/*********************************************************************/
//  IDT

#define MAX_IDT_NUM 0x80
uint16_t cs_sel;
x64_idt64_t* idt;

#define SET_SYSTEM_INT_HANDLER(num)  idt_set_kernel_handler(0x ## num, (uintptr_t)&_int ## num, 0)

void idt_set_handler(uint8_t num, uintptr_t offset, uint16_t sel, uint8_t ist) {
    x64_idt64_t desc;
    desc.offset_1   = offset;
    desc.sel        = sel;
    desc.ist        = ist;
    desc.attr       = 0x8E;
    desc.offset_2   = offset >> 16;
    desc.offset_3   = offset >> 32;
    desc.RESERVED   = 0;
    idt[num] = desc;
}

void idt_set_kernel_handler(uint8_t num, uintptr_t offset, uint8_t ist) {
    idt_set_handler(num, offset, cs_sel, ist);
}

void default_int_handler(x64_context_t* regs) {
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

void idt_init() {

    const size_t idt_size = MAX_IDT_NUM * sizeof(x64_idt64_t);
    idt = mm_alloc_static_pages(idt_size);
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
    //  TODO: nothing
}

void* PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(MOE_PHYSICAL_ADDRESS pa) {
    // TODO:
    return (void*)(pa);
}

uint8_t READ_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p) {
    volatile uint8_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    return *p;
}

void WRITE_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p, uint8_t v) {
    volatile uint8_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    *p = v;
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

#define IRQ_BASE    0x40
#define MAX_IRQ     24

IRQ_HANDLER irq_handler[MAX_IRQ];

#define IA32_APIC_BASE_MSR          0x1B
#define IA32_APIC_BASE_MSR_BSP      0x100
#define IA32_APIC_BASE_MSR_ENABLE   0x800

#define MAX_CPU 8
#define INVALID_CPUID   0xFF
typedef uint8_t apic_id_t;
int n_cpu = 0;
apic_id_t apic_ids[MAX_CPU];

MOE_PHYSICAL_ADDRESS lapic_base = 0;
MOE_PHYSICAL_ADDRESS ioapic_base   = 0;


// void apic_write_ioapic(uint8_t addr, uint32_t val) {
//     WRITE_PHYSICAL_UINT8(ioapic_base, addr);
//     WRITE_PHYSICAL_UINT32(ioapic_base + 0x10, val);
// }

// uint32_t apic_read_ioapic(uint8_t addr) {
//     WRITE_PHYSICAL_UINT8(ioapic_base, addr);
//     uint32_t retval = READ_PHYSICAL_UINT32(ioapic_base + 0x10);
//     return retval;
// }

void apic_set_io_redirect(uint8_t irq, uint8_t vector, uint8_t trigger, int mask, apic_id_t desination) {
    uint32_t redir_lo = vector | ((trigger & 0xA)<<12) | (mask ? 0x10000 : 0);
    WRITE_PHYSICAL_UINT8(ioapic_base, 0x10 + irq * 2);
    WRITE_PHYSICAL_UINT32(ioapic_base + 0x10, redir_lo);
    WRITE_PHYSICAL_UINT8(ioapic_base, 0x11 + irq * 2);
    WRITE_PHYSICAL_UINT32(ioapic_base + 0x10, desination << 24);
}

void apic_end_of_irq(uint8_t irq) {
    WRITE_PHYSICAL_UINT32(lapic_base + 0x0B0, 0);
}

void apic_enable_irq(uint8_t irq, uint8_t trigger, IRQ_HANDLER handler) {
    irq_handler[irq] = handler;
    apic_set_io_redirect(irq, IRQ_BASE + irq, trigger, 0, apic_ids[0]);
}

void apic_disable_irq(uint8_t irq) {
    apic_set_io_redirect(irq, 0, 0, 1, INVALID_CPUID);
    irq_handler[irq] = NULL;
}

void _irq_main(uint8_t irq, void* context) {
    IRQ_HANDLER handler = irq_handler[irq];
    if (handler) {
        handler(irq, context);
        apic_end_of_irq(irq);
    } else {
        apic_disable_irq(irq);
        //  TODO: Simulate GPF
        x64_context_t regs;
        regs.err = ((IRQ_BASE + irq) << 3) | ERROR_IDT | ERROR_EXT;
        regs.intnum = 0x0D;
        default_int_handler(&regs);
    }
}


void apic_init() {
    acpi_madt_t* madt = acpi_find_table(ACPI_MADT_SIGNATURE);
    if (madt) {

        //  Disable Legacy PIC
        if (madt->Flags & ACPI_MADT_PCAT_COMPAT) {
            __asm__ volatile (
                "outb %%al, $0x21\n"
                "outb %%al, $0xA1\n"
                ::"a"(0xFF));
        }

        //  Setup Local APIC
        uint64_t msr_lapic = io_rdmsr(IA32_APIC_BASE_MSR);
        msr_lapic |= IA32_APIC_BASE_MSR_ENABLE;
        io_wrmsr(IA32_APIC_BASE_MSR, msr_lapic);
        lapic_base = msr_lapic & ~0xFFF;

        apic_ids[n_cpu++] = READ_PHYSICAL_UINT32(lapic_base + 0x20) >> 24;

        //  Parse structures
        size_t max_length = madt->Header.length - 44;
        uint8_t* p = madt->Structure;
        for (size_t loc = 0; loc < max_length; ) {
            size_t len = p[loc+1];
            switch (p[loc]) {
            case 0x01: // IO APIC
                ioapic_base = *((uint32_t*)(p+loc+4));
                break;
            default:
                break;
            }
            loc += len;
        }

        //  Setup IO APIC

        //  Mask all IRQ
        for (int i = 0; i < 24; i++) {
            apic_disable_irq(i);
        }

        //  Install IDT handler
        idt_set_kernel_handler(IRQ_BASE, (uintptr_t)&_irq00, 0);
        idt_set_kernel_handler(IRQ_BASE+1, (uintptr_t)&_irq01, 0);
        idt_set_kernel_handler(IRQ_BASE+2, (uintptr_t)&_irq02, 0);

    }
}


/*********************************************************************/
//  High Precision Event Timer

MOE_PHYSICAL_ADDRESS hpet_base = 0;
uint32_t hpet_main_cnt_period = 0;
volatile uint64_t hpet_count = 0;

// void hpet_clear_interrupt(int timer_id) {
//     uint32_t v = READ_PHYSICAL_UINT32(hpet_base + 0x20);
//     uint32_t mask = (1 << timer_id);
//     v |= mask;
//     WRITE_PHYSICAL_UINT32(hpet_base + 0x020, v);
// }

int hpet_irq_handler(int irq, void* context) {
    hpet_count++;
    return 0;
}

uint64_t hpet_get_count() {
    return hpet_count;
}

void hpet_init() {
    acpi_hpet_t* hpet = acpi_find_table(ACPI_HPET_SIGNATURE);
    if (hpet) {
        hpet_base = hpet->address.address;

        hpet_main_cnt_period = READ_PHYSICAL_UINT32(hpet_base + 4);
        WRITE_PHYSICAL_UINT64(hpet_base + 0x10, 0);
        WRITE_PHYSICAL_UINT64(hpet_base + 0x20, 0); // Clear all interrupts
        WRITE_PHYSICAL_UINT64(hpet_base + 0xF0, 0); // Reset MAIN_COUNTER_VALUE
        WRITE_PHYSICAL_UINT64(hpet_base + 0x10, 0x03); // LEG_RT_CNF | ENABLE_CNF

        WRITE_PHYSICAL_UINT64(hpet_base + 0x100, 0x4C);
        WRITE_PHYSICAL_UINT64(hpet_base + 0x108, 10000000000000 / hpet_main_cnt_period);
        apic_enable_irq(0x02, 0x00, &hpet_irq_handler);

    } else {
        mgs_bsod();
        printf("PANIC: HPET_NOT_AVAILABLE\n");
        for (;;) __asm__ volatile ("hlt");
    }
}


/*********************************************************************/

#define PS2_STATE_RSHIFT    0x0001
#define PS2_STATE_LSHIFT    0x0002
#define PS2_STATE_CTRL      0x0004
#define PS2_STATE_ALT       0x0008
#define PS2_STATE_EXTEND    0x4000

#define SCANCODE_BREAK      0x80000000

#define PS2_SCAN_LCTRL      0x1D
#define PS2_SCAN_LSHIFT     0x2A
#define PS2_SCAN_LALT       0x38
#define PS2_SCAN_RSHIFT     0x36
#define PS2_SCAN_BREAK      0x80
#define PS2_SCAN_EXTEND     0xE0


static moe_ring_buffer_t ps2_buffer;
static uintptr_t ps2_state = 0;

int ps2_irq_handler(int irq, void* context) {
    while ((io_in8(0x64) & 0x21) == 0x01) {
        uint8_t data = io_in8(0x60);
        moe_ring_buffer_write(&ps2_buffer, data);
    }
    return 0;
}

static void ps2_set_shift(uint32_t state, uint32_t is_break) {
    if (is_break) {
        ps2_state &= ~state;
    } else {
        ps2_state |= state;
    }
}

uint32_t ps2_parse_scancode(uint32_t scancode) {
    if (scancode == PS2_SCAN_EXTEND) {
        ps2_state |= PS2_STATE_EXTEND;
        return 0;
    } else {
        uint32_t is_break = (scancode & PS2_SCAN_BREAK) ? SCANCODE_BREAK : 0;
        uint32_t scan = scancode & 0x7F;
        if (ps2_state & PS2_STATE_EXTEND) {
            ps2_state &= ~PS2_STATE_EXTEND;
            scan |= PS2_STATE_EXTEND;
        }
        switch (scan) {
            case PS2_SCAN_LSHIFT:
                ps2_set_shift(PS2_STATE_LSHIFT, is_break);
                break;
            case PS2_SCAN_RSHIFT:
                ps2_set_shift(PS2_STATE_RSHIFT, is_break);
                break;
            case PS2_SCAN_LCTRL:
                ps2_set_shift(PS2_STATE_CTRL, is_break);
                break;
            case PS2_SCAN_LALT:
                ps2_set_shift(PS2_STATE_ALT, is_break);
                break;
            default:
                return is_break | (ps2_state << 16) | scan;
        }
        return 0;
    }
}

uint32_t ps2_get_data() {
    uint8_t data = moe_ring_buffer_read(&ps2_buffer, 0);
    if (data) {
        return ps2_parse_scancode(data);
    } else {
        return 0;
    }
}

//  TODO: jp109 only
static uint8_t ps2_scan_table[] = {
    0, '\x1B', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '^', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '@', '[', '\r', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', ':', '`', 0, ']',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0,
};

uint32_t ps2_scan_to_unicode(uint32_t scancode) {
    if (scancode & SCANCODE_BREAK) return 0;
    uint32_t scan_lo = (scancode & 0x7F);
    uint32_t shift_state = scancode >> 16;
    uint32_t ascii_raw = 0;
    if (scan_lo < sizeof(ps2_scan_table)) {
        ascii_raw = ps2_scan_table[scan_lo];
    }
    if (ascii_raw >= 0x21 && ascii_raw <= 0x3F) {
        if (shift_state & (PS2_STATE_LSHIFT | PS2_STATE_RSHIFT)) {
            ascii_raw ^= 0x10;
        }
    } else if (ascii_raw >= 0x40 && ascii_raw <= 0x7E) {
        if (shift_state & PS2_STATE_CTRL) {
            ascii_raw &= 0x1F;
        } else if (shift_state & (PS2_STATE_LSHIFT | PS2_STATE_RSHIFT)) {
            ascii_raw ^= 0x20;
        }
    }
    return ascii_raw;
}

void ps2_init() {

    uintptr_t size_of_buffer = 16;
    intptr_t* buffer = mm_alloc_static(size_of_buffer * sizeof(intptr_t));
    moe_ring_buffer_init(&ps2_buffer, buffer, size_of_buffer);

    apic_enable_irq(1, 0, ps2_irq_handler);

}


/*********************************************************************/

void arch_init() {
    page_init();
    cs_sel = gdt_init();
    idt_init();
    apic_init();
    hpet_init();
    __asm__ volatile("sti");

    ps2_init();
}

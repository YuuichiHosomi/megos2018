// Minimal Architecture Specific Initialization
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "x86.h"


extern _Atomic uint32_t *smp_setup_init(uint8_t vector_sipi, int max_cpu, size_t stack_chunk_size, uintptr_t* stacks);
extern uint16_t gdt_init(void);
extern void idt_load(volatile void*, size_t);
extern void* _int00;
extern void* _int03;
extern void* _int06;
extern void* _int07;
extern void* _int0D;
extern void* _int0E;
extern void* _irq00;
extern void* _irq01;
extern void* _irq02;
extern void* _irq0C;
extern void* _ipi_sche;
uint64_t io_rdmsr(uint32_t addr);
void io_wrmsr(uint32_t addr, uint64_t val);

void io_out8(uint16_t port, uint8_t val);
uint8_t io_in8(uint16_t port);
void io_out32(uint16_t port, uint32_t val);
uint32_t io_in32(uint16_t port);

void thread_init(int n_active_cpu);
void reschedule();


/*********************************************************************/
//  IDT

#define MAX_IDT_NUM 0x100
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
    printf("#### EXCEPTION %02llx ERR %04llx-%016llx\n", regs->intnum, regs->err, regs->cr2);
    printf("ThreadID %d IP %04llx:%016llx SP %04llx:%016llx FL %08llx\n", moe_get_current_thread(), regs->cs, regs->rip, regs->ss, regs->rsp, regs->rflags);
    printf(
        "ABCD %016llx %016llx %016llx %016llx\n"
        "BPSD %016llx %016llx %016llx\n"
        "R8-  %016llx %016llx %016llx %016llx\n"
        "R12- %016llx %016llx %016llx %016llx\n",
        regs->rax, regs->rbx, regs->rcx, regs->rdx, regs->rbp, regs->rsi, regs->rdi,
        regs->r8, regs->r9, regs->r10, regs->r11, regs->r12, regs->r13, regs->r14, regs->r15);

    moe_exit_thread(-1);
}

void idt_init() {

    const size_t idt_size = MAX_IDT_NUM * sizeof(x64_idt64_t);
    idt = mm_alloc_static_page(idt_size);
    memset((void*)idt, 0, idt_size);

    SET_SYSTEM_INT_HANDLER(00); // #DE Divide by zero Error
    SET_SYSTEM_INT_HANDLER(03); // #BP Breakpoint
    SET_SYSTEM_INT_HANDLER(06); // #UD Undefined Opcode
    SET_SYSTEM_INT_HANDLER(07); // #NM Device not Available
    SET_SYSTEM_INT_HANDLER(0D); // #GP General Protection Fault
    SET_SYSTEM_INT_HANDLER(0E); // #PF Page Fault

    idt_load(idt, idt_size-1);
}


/*********************************************************************/
//  Advanced Programmable Interrupt Controller

#define IRQ_BASE                    0x40
#define MAX_IRQ                     24
#define MAX_CPU                     32
#define INVALID_CPUID               0xFF
#define IRQ_SCHDULE                 0xFC

#define IA32_APIC_BASE_MSR          0x1B
#define IA32_APIC_BASE_MSR_BSP      0x100
#define IA32_APIC_BASE_MSR_ENABLE   0x800

// type 00 Processor Local APIC
typedef struct {
    uint8_t     acpi_uid;
    uint8_t     apic_id;
    uint32_t    flags;
} __attribute__((packed)) apic_madt_lapic_t;

// type 01 I/O APIC
typedef struct {
    uint8_t     ioapic_id;
    uint8_t     RESERVED;
    uint32_t    ioapic_base;
    uint32_t    gsi_base;
} __attribute__((packed)) apic_madt_ioapic_t;

//  type 02 Interrupt Source Override Structure
typedef struct {
    uint8_t     bus;
    uint8_t     irq;
    uint32_t    gsi;
    uint16_t    flags;
} __attribute__((packed)) apic_madt_ovr_t;

typedef uint8_t apic_id_t;

IRQ_HANDLER irq_handler[MAX_IRQ];
apic_madt_ovr_t gsi_table[MAX_IRQ];

apic_id_t apic_ids[MAX_CPU];
uint8_t apicid_to_cpuids[256];
int n_cpu = 0;
MOE_PHYSICAL_ADDRESS lapic_base = 0;
MOE_PHYSICAL_ADDRESS ioapic_base   = 0;
int smp_mode = 0;


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

apic_madt_ovr_t get_madt_ovr(uint8_t irq) {
    apic_madt_ovr_t retval = gsi_table[irq];
    if (retval.bus == 0) {
        return retval;
    } else { // Default
        apic_madt_ovr_t def_val = { 0, irq, irq, 0 };
        return def_val;
    }
}

void apic_enable_irq(uint8_t irq, IRQ_HANDLER handler) {
    apic_madt_ovr_t ovr = get_madt_ovr(irq);
    irq_handler[ovr.gsi] = handler;
    apic_set_io_redirect(ovr.gsi, IRQ_BASE + ovr.gsi, ovr.flags, 0, apic_ids[0]);
}

void apic_disable_irq(uint8_t irq) {
    apic_madt_ovr_t ovr = get_madt_ovr(irq);
    apic_set_io_redirect(ovr.gsi, 0, 0, 1, INVALID_CPUID);
    irq_handler[ovr.gsi] = NULL;
}

void _irq_main(uint8_t irq, void* p) {
    IRQ_HANDLER handler = irq_handler[irq];
    if (handler) {
        handler(irq, p);
        apic_end_of_irq(irq);
    } else {
        apic_disable_irq(irq);
        //  TODO: Simulate GPF
        x64_context_t regs;
        regs.err = ((IRQ_BASE + irq) << 3) | ERROR_IDT | ERROR_EXT;
        regs.intnum = 0x0D;
        default_int_handler(&regs);
    }
    if (irq == 2) {
        reschedule();
        if (smp_mode) {
            WRITE_PHYSICAL_UINT32(lapic_base + 0x300, 0xC0000 + IRQ_SCHDULE);
        }
    }
}

void ipi_sche_main() {
    WRITE_PHYSICAL_UINT32(lapic_base + 0x0B0, 0);
    reschedule();
}

uintptr_t moe_get_current_cpuid() {
    int apicid = (READ_PHYSICAL_UINT32(lapic_base + 0x20) >> 24);
    return apicid_to_cpuids[apicid];
}

void apic_init_ap(uint8_t cpuid) {
    uint64_t msr_lapic = io_rdmsr(IA32_APIC_BASE_MSR);
    msr_lapic |= IA32_APIC_BASE_MSR_ENABLE;
    io_wrmsr(IA32_APIC_BASE_MSR, msr_lapic);

    uint8_t apicid = READ_PHYSICAL_UINT32(lapic_base + 0x20) >> 24;
    apicid_to_cpuids[apicid] = cpuid;

    WRITE_PHYSICAL_UINT32(lapic_base + 0x0F0, 0x10F);
}


void apic_init() {
    acpi_madt_t* madt = acpi_find_table(ACPI_MADT_SIGNATURE);
    if (madt) {

        //  Init IRQ table
        memset(gsi_table, -1, sizeof(gsi_table));

        memset(apicid_to_cpuids, 0, 256);

        // apic_madt_ovr_t gsi_irq00 = { 0, 0, 2, 0 };
        // gsi_table[0] = gsi_irq00;

        apic_madt_ovr_t gsi_irq01 = { 0, 1, 1, 0 };
        gsi_table[1] = gsi_irq01;

        //  Setup Local APIC
        uint64_t msr_lapic = io_rdmsr(IA32_APIC_BASE_MSR);
        msr_lapic |= IA32_APIC_BASE_MSR_ENABLE;
        io_wrmsr(IA32_APIC_BASE_MSR, msr_lapic);
        lapic_base = msr_lapic & ~0xFFF;

        apic_ids[n_cpu++] = READ_PHYSICAL_UINT32(lapic_base + 0x20) >> 24;

        // WRITE_PHYSICAL_UINT32(lapic_base + 0x0F0, 0x100);

        //  Parse structures
        size_t max_length = madt->Header.length - 44;
        uint8_t* p = madt->Structure;
        for (size_t loc = 0; loc < max_length; ) {
            size_t len = p[loc+1];
            void* madt_structure = (void*)(p+loc+2);
            switch (p[loc]) {

            case 0x00: // Processor Local APIC
            {
                if (n_cpu < MAX_CPU) {
                    apic_madt_lapic_t* lapic = madt_structure;
                    if ((lapic->flags & 1) && apic_ids[0] != lapic->apic_id) {
                        apic_ids[n_cpu++] = lapic->apic_id;
                    }
                }
            }
                break;

            case 0x01: // IO APIC
            {
                apic_madt_ioapic_t* ioapic = madt_structure;
                if (ioapic->gsi_base == 0) {
                    ioapic_base = ioapic->ioapic_base;
                }
            }
                break;

            case 0x02: // Interrupt Source Override
            {
                apic_madt_ovr_t* ovr = madt_structure;
                gsi_table[ovr->irq] = *ovr;
            }
                break;

            default:
                break;
            }
            loc += len;
        }

        //  Setup IO APIC

        //  Mask all IRQ
        for (int i = 0; i < MAX_IRQ; i++) {
            apic_disable_irq(i);
        }

        //  Install IDT handler
        idt_set_kernel_handler(IRQ_BASE, (uintptr_t)&_irq00, 0);
        idt_set_kernel_handler(IRQ_BASE+1, (uintptr_t)&_irq01, 0);
        idt_set_kernel_handler(IRQ_BASE+2, (uintptr_t)&_irq02, 0);
        idt_set_kernel_handler(IRQ_BASE+12, (uintptr_t)&_irq0C, 0);
        idt_set_kernel_handler(IRQ_SCHDULE, (uintptr_t)&_ipi_sche, 0);

        //  Disable Legacy PIC
        if (madt->Flags & ACPI_MADT_PCAT_COMPAT) {
            __asm__ volatile (
                // "cli\n"
                "movb $0xFF, %%al\n"
                "outb %%al, $0xA1\n"
                "outb %%al, $0x21\n"
                :::"%al");
        }
        __asm__ volatile("sti");

    }
}


//  because to initialize AP needs Timer
void apic_init_mp() {
    if (n_cpu > 1) {
        uint8_t vector_sipi = 0x10;
        const uintptr_t stack_chunk_size = 0x4000;
        uintptr_t* stacks = mm_alloc_static_page(stack_chunk_size * n_cpu);
        _Atomic uint32_t* wait_p = smp_setup_init(vector_sipi, MAX_CPU, stack_chunk_size, stacks);
        WRITE_PHYSICAL_UINT32(lapic_base + 0x300, 0x000C4500);
        moe_usleep(10000);
        WRITE_PHYSICAL_UINT32(lapic_base + 0x300, 0x000C4600 + vector_sipi);
        moe_usleep(200000);
        thread_init(atomic_load(wait_p));
        smp_mode = 1;
    } else {
        thread_init(1);
    }
}


/*********************************************************************/
//  High Precision Event Timer

#define HPET_DIV    5
MOE_PHYSICAL_ADDRESS hpet_base = 0;
uint32_t hpet_main_cnt_period = 0;
volatile uint64_t hpet_count = 0;
static const uint64_t timer_div = 1000 * HPET_DIV;
uint64_t measure_div;

int hpet_irq_handler(int irq, void* context) {
    hpet_count++;
    return 0;
}

moe_timer_t moe_create_interval_timer(uint64_t us) {
    return (us / timer_div) + hpet_count + 1;
}

int moe_check_timer(moe_timer_t* timer) {
    return ((intptr_t)(*timer - hpet_count) > 0);
}

uint64_t moe_get_measure() {
    uint64_t main_cnt = READ_PHYSICAL_UINT64(hpet_base + 0xF0);
    return main_cnt / measure_div;
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

        measure_div = 1000000000 / hpet_main_cnt_period;
        WRITE_PHYSICAL_UINT64(hpet_base + 0x100, 0x4C);
        WRITE_PHYSICAL_UINT64(hpet_base + 0x108, HPET_DIV * 1000000000000 / hpet_main_cnt_period);
        apic_enable_irq(0, &hpet_irq_handler);

    } else {
        //  TODO: impl APIC timer?
        MOE_ASSERT(false, "FATAL: HPET_NOT_AVAILABLE\n");
    }
}


/*********************************************************************/
//  Peripheral Component Interconnect

#define PCI_CONFIG_ADDRESS  0x0CF8
#define PCI_CONFIG_DATA     0x0CFC
#define PCI_ADDRESS_ENABLE  0x80000000

uint32_t pci_get_register_address(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    return (reg & 0xFC) | ((func) << 8) | ((dev) << 11) | (bus << 16);
}

uint32_t pci_read_config_register(uint32_t base, uint8_t reg) {
    io_out32(PCI_CONFIG_ADDRESS, PCI_ADDRESS_ENABLE | base | reg);
    uint32_t retval = io_in32(PCI_CONFIG_DATA);
    io_out32(PCI_CONFIG_ADDRESS, 0);
    return retval;
}

void pci_write_config_register(uint32_t base, uint8_t reg, uint32_t val) {
    io_out32(PCI_CONFIG_ADDRESS, PCI_ADDRESS_ENABLE | base | reg);
    io_out32(PCI_CONFIG_DATA, val);
    io_out32(PCI_CONFIG_ADDRESS, 0);
}



/*********************************************************************/
//  PS/2 Keyboard and Mouse
#include "hid.h"

#define PS2_DATA_PORT       0x0060
#define PS2_STATUS_PORT     0x0064
#define PS2_COMMAND_PORT    0x0064

#define PS2_STATE_EXTEND    0x4000

#define SCANCODE_BREAK      0x80000000

#define PS2_SCAN_LCTRL      0x1D
#define PS2_SCAN_LSHIFT     0x2A
#define PS2_SCAN_LALT       0x38
#define PS2_SCAN_RSHIFT     0x36
#define PS2_SCAN_BREAK      0x80
#define PS2_SCAN_EXTEND     0xE0
#define PS2_SCAN_EXT16      0x80
#define PS2_SCAN_RCTRL      (PS2_SCAN_EXT16|PS2_SCAN_LCTRL)
#define PS2_SCAN_RALT       (PS2_SCAN_EXT16|PS2_SCAN_LALT)
#define PS2_SCAN_LGUI       (PS2_SCAN_EXT16|0x5B)
#define PS2_SCAN_RGUI       (PS2_SCAN_EXT16|0x5C)

#define PS2_TIMEOUT         1000

static moe_fifo_t* ps2k_buffer;
static moe_fifo_t* ps2m_buffer;

static volatile uintptr_t ps2k_state = 0;

typedef enum {
    ps2m_phase_ack = 0,
    ps2m_phase_head,
    ps2m_phase_x,
    ps2m_phase_y,
} ps2m_packet_phase;

ps2m_packet_phase ps2m_phase;
uint8_t ps2m_packet[4];

uint8_t ps2_to_hid_scan_table[] = {
    0x00, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B, // 0
    0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30, 0x28, 0xE0, 0x04, 0x16, // 1
    0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x35, 0xE1, 0x31, 0x1D, 0x1B, 0x06, 0x19, // 2
    0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xE5, 0x55, 0xE2, 0x2C, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, // 3
    0x3F, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5F, 0x60, 0x61, 0x56, 0x5C, 0x5D, 0x5E, 0x57, 0x59, // 4
    0x5A, 0x5B, 0x62, 0x63,    0,    0,    0, 0x44, 0x45,    0,    0,    0,    0,    0,    0,    0, // 5
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // 6
    0x88,    0,    0, 0x87,    0,    0,    0,    0,    0, 0x8A,    0, 0x8B,    0, 0x89,    0,    0, // 7
//     0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // E0 0
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x58, 0xE4,    0,    0, // E0 1
    0x7F,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x81,    0, // E0 2
    0x80,    0,    0,    0,    0, 0x54,    0,    0, 0xE6,    0,    0,    0,    0,    0,    0,    0, // E0 3
       0,    0,    0,    0,    0,    0,    0, 0x4A, 0x52, 0x4B,    0, 0x50,    0, 0x4F,    0, 0x4D, // E0 4
    0x51, 0x4E, 0x49, 0x4C,    0,    0,    0,    0,    0,    0,    0, 0xE3, 0xE7, 0x65, 0x66,    0, // E0 5
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // E0 6
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // E0 7
};

int ps2_wait_for_write(uint64_t timeout) {
    moe_timer_t timer = moe_create_interval_timer(timeout);
    while (moe_check_timer(&timer)) {
        if ((io_in8(PS2_STATUS_PORT) & 0x02) == 0x00) {
            return 0;
        }
        io_pause();
    }
    return -1;
}

int ps2_irq_handler(int irq, void* context) {
    uint8_t ps2_status;
    while((ps2_status = io_in8(PS2_STATUS_PORT)) & 0x01) {
        if (ps2_status & 0x20) {
            moe_fifo_write(ps2m_buffer, io_in8(PS2_DATA_PORT));
        } else {
            moe_fifo_write(ps2k_buffer, io_in8(PS2_DATA_PORT));
        }
    }
    return 0;
}

static void ps2_set_modifier(uint32_t state, uint32_t is_break) {
    if (is_break) {
        ps2k_state &= ~state;
    } else {
        ps2k_state |= state;
    }
}

int ps2_parse_data(moe_hid_keyboard_report_t* keyreport, moe_hid_mouse_report_t* mouse_report) {

    int m = moe_fifo_read(ps2m_buffer, -1);
    if (m >= 0) {
        switch(ps2m_phase) {
            case ps2m_phase_ack:
                if (m == 0xFA) ps2m_phase++;
                return 0;

            case ps2m_phase_head:
                if ((m &0xC8) == 0x08) {
                    ps2m_packet[ps2m_phase++] = m;
                }
                return 3;

            case ps2m_phase_x:
                ps2m_packet[ps2m_phase++] = m;
                return 3;

            case ps2m_phase_y:
                ps2m_packet[ps2m_phase] = m;
                ps2m_phase = ps2m_phase_head;

                mouse_report->buttons = ps2m_packet[1] & 0x07;
                int32_t x, y;
                x = ps2m_packet[2];
                y = ps2m_packet[3];
                if (ps2m_packet[1] & 0x10) {
                    x |= 0xFFFFFF00;
                }
                if (ps2m_packet[1] & 0x20) {
                    y |= 0xFFFFFF00;
                }
                y = 0 - y;
                mouse_report->x = x;
                mouse_report->y = y;

                return 2;
        }
    }

    uint8_t data = moe_fifo_read(ps2k_buffer, 0);
    if (data == PS2_SCAN_EXTEND) {
        ps2k_state |= PS2_STATE_EXTEND;
        return 3;
    } else if (data) {
        memset(keyreport->keydata, 0, 6);
        uint32_t is_break = (data & PS2_SCAN_BREAK) ? SCANCODE_BREAK : 0;
        uint32_t scan = data & 0x7F;
        if (ps2k_state & PS2_STATE_EXTEND) {
            ps2k_state &= ~PS2_STATE_EXTEND;
            scan |= PS2_SCAN_EXT16;
        }
        switch (scan) {
            case PS2_SCAN_LSHIFT:
                ps2_set_modifier(HID_MOD_LSHIFT, is_break);
                break;
            case PS2_SCAN_RSHIFT:
                ps2_set_modifier(HID_MOD_LSHIFT, is_break);
                break;
            case PS2_SCAN_LCTRL:
                ps2_set_modifier(HID_MOD_LCTRL, is_break);
                break;
            case PS2_SCAN_LALT:
                ps2_set_modifier(HID_MOD_LALT, is_break);
                break;
            case PS2_SCAN_RCTRL:
                ps2_set_modifier(HID_MOD_RCTRL, is_break);
                break;
            case PS2_SCAN_RALT:
                ps2_set_modifier(HID_MOD_RALT, is_break);
                break;
            case PS2_SCAN_LGUI:
                ps2_set_modifier(HID_MOD_LGUI, is_break);
                break;
            case PS2_SCAN_RGUI:
                ps2_set_modifier(HID_MOD_RGUI, is_break);
                break;
            default:
                if (!is_break) {
                    keyreport->keydata[0] = ps2_to_hid_scan_table[scan];
                }
        }
        keyreport->modifier = ps2k_state & 0xFF;
        return 1;
    }

    return 0;
}

int ps2_init() {
    if (!ps2_wait_for_write(100000)){
        io_out8(PS2_COMMAND_PORT, 0xAD);
        ps2_wait_for_write(PS2_TIMEOUT);
        io_out8(PS2_COMMAND_PORT, 0xA7);

        for (int i = 0; i< 16; i++) {
            io_in8(PS2_DATA_PORT);
        }

        ps2_wait_for_write(PS2_TIMEOUT);
        io_out8(PS2_COMMAND_PORT, 0x60);
        ps2_wait_for_write(PS2_TIMEOUT);
        io_out8(PS2_DATA_PORT, 0x47);

        ps2_wait_for_write(PS2_TIMEOUT);
        io_out8(PS2_COMMAND_PORT, 0xD4);
        ps2_wait_for_write(PS2_TIMEOUT);
        io_out8(PS2_DATA_PORT, 0xF4);

        uintptr_t size_of_buffer = 128;
        ps2k_buffer = moe_fifo_init(size_of_buffer);
        ps2m_buffer = moe_fifo_init(size_of_buffer);

        apic_enable_irq(1, ps2_irq_handler);
        apic_enable_irq(12, ps2_irq_handler);

        return 1;
    }
    return 0;
}


/*********************************************************************/

void arch_init() {
    cs_sel = gdt_init();
    idt_init();
    apic_init();
    hpet_init();
    apic_init_mp();
}

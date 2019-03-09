// Architecture Specific
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
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

extern void* _int00;
extern void* _int03;
extern void* _int06;
extern void* _int07;
extern void* _int0D;
extern void* _int0E;
extern void* _irq00;
extern void* _irq01;
extern void* _irq02;
extern void* _irq09;
extern void* _irq0C;
extern void* _ipi_sche;

extern uint16_t gdt_init(void);
extern void idt_load(volatile void*, size_t);
extern _Atomic uint32_t *smp_setup_init(uint8_t vector_sipi, int max_cpu, size_t stack_chunk_size, uintptr_t* stacks);
extern void thread_init(int n_active_cpu);
extern void reschedule();


static void io_out32(uint16_t const port, uint32_t val) {
    __asm__ volatile("outl %%eax, %%dx": : "d"(port), "a"(val));
}

static uint32_t io_in32(uint16_t const port) {
    uint32_t eax;
    __asm__ volatile("inl %%dx, %%eax": "=a"(eax): "d"(port));
    return eax;
}

static tuple_eax_edx_t io_rdmsr(uint32_t const addr) {
    tuple_eax_edx_t result;
    __asm__ volatile ("rdmsr": "=a"(result.eax), "=d"(result.edx) : "c"(addr));
    return result;
}

static void io_wrmsr(uint32_t const addr, tuple_eax_edx_t val) {
    __asm__ volatile ("wrmsr": : "c"(addr), "d"(val.edx), "a"(val.eax));
}


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
    char buff[1024];

    snprintf(buff, 1024,
        "Something went wrong.\n\n"
        "TECH INFO: EXC-%02llx-%04llx-%016llx\n"
        "Thread %d IP %04llx:%016llx SP %04llx:%016llx FL %08llx\n"
        "ABCD %016llx %016llx %016llx %016llx\n"
        "BPSD %016llx %016llx %016llx\n"
        "R8-  %016llx %016llx %016llx %016llx\n"
        "R12- %016llx %016llx %016llx %016llx\n"
        , regs->intnum, regs->err, regs->cr2
        , moe_get_current_thread(), regs->cs, regs->rip, regs->ss, regs->rsp, regs->rflags
        , regs->rax, regs->rbx, regs->rcx, regs->rdx, regs->rbp, regs->rsi, regs->rdi
        , regs->r8, regs->r9, regs->r10, regs->r11, regs->r12, regs->r13, regs->r14, regs->r15
        );

    mgs_bsod(buff);
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

MOE_IRQ_HANDLER irq_handler[MAX_IRQ];
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

int moe_enable_irq(uint8_t irq, MOE_IRQ_HANDLER handler) {
    apic_madt_ovr_t ovr = get_madt_ovr(irq);
    irq_handler[ovr.gsi] = handler;
    apic_set_io_redirect(ovr.gsi, IRQ_BASE + ovr.gsi, ovr.flags, 0, apic_ids[0]);
    return 0;
}

int moe_disable_irq(uint8_t irq) {
    apic_madt_ovr_t ovr = get_madt_ovr(irq);
    apic_set_io_redirect(ovr.gsi, 0, 0, 1, INVALID_CPUID);
    irq_handler[ovr.gsi] = NULL;
    return 0;
}

void _irq_main(uint8_t irq, void* p) {
    MOE_IRQ_HANDLER handler = irq_handler[irq];
    if (handler) {
        handler(irq);
        apic_end_of_irq(irq);
    } else {
        moe_disable_irq(irq);
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
    tuple_eax_edx_t msr_lapic = io_rdmsr(IA32_APIC_BASE_MSR);
    msr_lapic.u64 |= IA32_APIC_BASE_MSR_ENABLE;
    io_wrmsr(IA32_APIC_BASE_MSR, msr_lapic);

    uint8_t apicid = READ_PHYSICAL_UINT32(lapic_base + 0x20) >> 24;
    apicid_to_cpuids[apicid] = cpuid;

    WRITE_PHYSICAL_UINT32(lapic_base + 0x0F0, 0x10F);
}


void apic_init() {
    acpi_madt_t* madt = acpi_find_table(ACPI_MADT_SIGNATURE);
    if (madt) {

        //  Disable Legacy PIC
        if (madt->Flags & ACPI_MADT_PCAT_COMPAT) {
            __asm__ volatile(
                "mov $0xFF, %%al;\n"
                "outb %%al, $0xA1;\n"
                "pause;\n"
                "outb %%al, $0x21;\n"
                :::"al");
        }

        //  Init IRQ table
        memset(gsi_table, -1, sizeof(gsi_table));

        memset(apicid_to_cpuids, 0, 256);

        // apic_madt_ovr_t gsi_irq00 = { 0, 0, 2, 0 };
        // gsi_table[0] = gsi_irq00;

        apic_madt_ovr_t gsi_irq01 = { 0, 1, 1, 0 };
        gsi_table[1] = gsi_irq01;

        //  Setup Local APIC
        tuple_eax_edx_t msr_lapic = io_rdmsr(IA32_APIC_BASE_MSR);
        msr_lapic.u64 |= IA32_APIC_BASE_MSR_ENABLE;
        io_wrmsr(IA32_APIC_BASE_MSR, msr_lapic);
        lapic_base = msr_lapic.u64 & ~0xFFF;

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
            moe_disable_irq(i);
        }

        //  Install IDT handler
        idt_set_kernel_handler(IRQ_BASE, (uintptr_t)&_irq00, 0);
        idt_set_kernel_handler(IRQ_BASE+1, (uintptr_t)&_irq01, 0);
        idt_set_kernel_handler(IRQ_BASE+2, (uintptr_t)&_irq02, 0);
        idt_set_kernel_handler(IRQ_BASE+9, (uintptr_t)&_irq09, 0);
        idt_set_kernel_handler(IRQ_BASE+12, (uintptr_t)&_irq0C, 0);
        idt_set_kernel_handler(IRQ_SCHDULE, (uintptr_t)&_ipi_sche, 0);

        // Then enable IRQ
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

int hpet_irq_handler(int irq) {
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
        moe_enable_irq(0, &hpet_irq_handler);

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

extern void acpi_init_sci();
void arch_init() {
    cs_sel = gdt_init();
    idt_init();
    apic_init();
    hpet_init();
    acpi_init_sci();
    apic_init_mp();
}

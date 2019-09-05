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

typedef struct cpuid_t {
    uint32_t eax, ecx, edx, ebx;
} cpuid_t;

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


#define SIZE_TSS    0x10000
extern uint16_t gdt_init();
extern void gdt_load(void *gdt, x64_tss_desc_t *tss);
extern void idt_load(volatile void*, size_t);
extern x64_tss_t *io_get_tss();
extern _Atomic uint32_t *smp_setup_init(uint8_t vector_sipi, int max_cpu, size_t stack_chunk_size, uintptr_t* stack_base);
extern void io_set_lazy_fpu_restore();
extern void thread_init(int);
extern void thread_reschedule();
extern void lpc_init();
extern int acpi_enable(int enabled);


static tuple_eax_edx_t io_rdmsr(uint32_t const addr) {
    tuple_eax_edx_t result;
    __asm__ volatile ("rdmsr": "=a"(result.eax), "=d"(result.edx) : "c"(addr));
    return result;
}

static void io_wrmsr(uint32_t const addr, tuple_eax_edx_t val) {
    __asm__ volatile ("wrmsr": : "c"(addr), "d"(val.edx), "a"(val.eax));
}

static void io_cpuid(cpuid_t *regs) {
    __asm__ volatile ("cpuid"
    : "=a"(regs->eax), "=c"(regs->ecx), "=d"(regs->edx), "=b"(regs->ebx)
    : "a"(regs->eax), "c"(regs->ecx)
    );
}


/*********************************************************************/
//  IDT - Interrupt Descriptor Table

#define MAX_IDT_NUM 0x100
uint16_t cs_sel;
x64_idt64_t* idt;


void idt_set_handler(uint8_t num, uintptr_t offset, uint16_t sel, uint8_t attr, uint8_t ist) {
    x64_idt64_t desc = {{0}};
    desc.offset_1   = offset;
    desc.sel        = sel;
    desc.ist        = ist;
    desc.attr       = attr;
    desc.offset_2   = offset >> 16;
    desc.offset_3   = offset >> 32;
    idt[num] = desc;
}

static void idt_set_kernel_handler(uint8_t num, uintptr_t offset, uint8_t ist) {
    idt_set_handler(num, offset, cs_sel, 0x8E, ist);
}

static void tss_init(x64_tss_desc_t *tss, uint64_t base, size_t size) {
    x64_tss_desc_t _tss = {{size - 1}};
    _tss.base_1 = base;
    _tss.base_2 = base >> 24;
    _tss.base_3 = base >> 32;
    _tss.type = DESC_TYPE_TSS64;
    _tss.present = 1;
    *tss = _tss;
}

static void gdt_setup() {
    uintptr_t tss_base = (uintptr_t)moe_alloc_object(SIZE_TSS, 1);
    x64_tss_desc_t tss;
    tss_init(&tss, tss_base, SIZE_TSS);

    // const size_t ist_size = 0x4000;
    // for (int i = 0; i < 7; i++) {
    //     uintptr_t ist = (uintptr_t)moe_alloc_object(ist_size, 1) + ist_size;
    //     tss->IST[i] = ist;
    // }

    void *gdt = moe_alloc_object(4096, 1);
    gdt_load(gdt, &tss);
}

#define BSOD_BUFF_SIZE 1024
static char bsod_buff[BSOD_BUFF_SIZE];
extern int putchar(int);
extern void gs_bsod();
void default_int_handler(x64_context_t* regs) {
    static moe_spinlock_t lock;
    moe_spinlock_acquire(&lock);

    snprintf(bsod_buff, BSOD_BUFF_SIZE,
        "#### EXCEPTION on PID %d thread %d: %s (fiber:%d %s)\n"
        "ERR %02llx-%04llx-%016llx IP %02llx:%012llx F %08llx\n"
        "AX %016llx BX %016llx CX %016llx DX %016llx\n"
        "SP %012llx BP %016llx SI %016llx DI %016llx\n"
        "R8- %016llx %016llx %016llx %016llx\n"
        "R12- %016llx %016llx %016llx %016llx\n"

        , moe_get_pid()
        , moe_get_current_thread_id(), moe_get_current_thread_name()
        , moe_get_current_fiber_id(), moe_get_current_fiber_name()
        , regs->intnum, regs->err, regs->cr2, regs->cs, regs->rip, regs->rflags
        , regs->rax, regs->rbx, regs->rcx, regs->rdx, regs->rsp, regs->rbp, regs->rsi, regs->rdi
        , regs->r8, regs->r9, regs->r10, regs->r11, regs->r12, regs->r13, regs->r14, regs->r15
        );

    gs_bsod();
    for (const char *p = bsod_buff; *p; p++) {
        putchar(*p);
    }
    moe_spinlock_release(&lock);
    moe_exit_thread(-1);
}


static void idt_init() {

    const size_t idt_size = MAX_IDT_NUM * sizeof(x64_idt64_t);
    idt = moe_alloc_object(idt_size, 1);

    idt_set_kernel_handler(0x00, (uintptr_t)&_int00, 0); // #DE Divide Error
    idt_set_kernel_handler(0x03, (uintptr_t)&_int03, 0); // #BP Breakpoint
    idt_set_kernel_handler(0x06, (uintptr_t)&_int06, 0); // #UD Undefined Opcode
    idt_set_kernel_handler(0x07, (uintptr_t)&_int07, 0); // #NM Device Not Available
    idt_set_kernel_handler(0x08, (uintptr_t)&_int08, 0); // #DF Double Fault
    idt_set_kernel_handler(0x0D, (uintptr_t)&_int0D, 0); // #GP General Protection Fault
    idt_set_kernel_handler(0x0E, (uintptr_t)&_int0E, 0); // #PF Page Fault

    idt_load(idt, idt_size - 1);
}


/*********************************************************************/
//  Advanced Programmable Interrupt Controller

#define IRQ_BASE                    0x40
#define MAX_IOAPIC_IRQ              24
#define MAX_MSI                     24
#define MAX_IRQ                     (MAX_IOAPIC_IRQ + MAX_MSI)
#define IRQ_INVALIDATE_TLB          0xEE
#define IRQ_SCHEDULE                0xFC
#define IRQ_LAPIC_TIMER             IRQ_BASE

#define MAX_CPU                     32
#define INVALID_CPUID               0xFF

#define IA32_APIC_BASE_MSR          0x0000001B
#define IA32_APIC_BASE_MSR_BSP      0x00000100
#define IA32_APIC_BASE_MSR_ENABLE   0x00000800
#define IA32_TSC_AUX_MSR            0xC0000103

#define MSI_BASE                    0xFEE00000

#define APIC_REDIR_MASK             0x00010000


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
void *ioapic_base = NULL;
_Atomic int smp_mode = 0;
_Atomic uint8_t next_msi = 0;

uint32_t lapic_timer_div = 0;
uint32_t lapic_timer_div2 = 0;
_Atomic uint64_t lapic_timer_value = 0;


static void apic_write_ioapic(int _index, uint32_t value) {
    _Atomic uint32_t *index = (void *)((uintptr_t)ioapic_base);
    _Atomic uint32_t *data = (void *)((uintptr_t)ioapic_base + 0x10);
    *index = _index;
    *data = value;
}

static uint32_t apic_read_ioapic(int _index) {
    _Atomic uint32_t *index = (void *)((uintptr_t)ioapic_base);
    _Atomic uint32_t *data = (void *)((uintptr_t)ioapic_base + 0x10);
    *index = _index;
    return *data;
}

static void apic_set_io_redirect(uint8_t irq, uint8_t vector, uint8_t trigger, int mask, apic_id_t destination) {
    uint32_t redir_lo = vector | ((trigger & 0xA)<<12) | (mask ? APIC_REDIR_MASK : 0);
    apic_write_ioapic(0x10 + irq * 2, redir_lo);
    apic_write_ioapic(0x11 + irq * 2, destination << 24);
}

static void apic_write_lapic(int index, uint32_t value) {
    WRITE_PHYSICAL_UINT32(lapic_base + index, value);
}

static uint32_t apic_read_lapic(int index) {
    return READ_PHYSICAL_UINT32(lapic_base + index);
}

static void apic_end_of_irq(uint8_t irq) {
    apic_write_lapic(0x0B0, 0);
}

static apic_madt_ovr_t get_madt_ovr(uint8_t irq) {
    apic_madt_ovr_t retval = gsi_table[irq];
    if (retval.bus == 0) {
        return retval;
    } else { // Default
        apic_madt_ovr_t def_val = { 0, irq, irq, 0 };
        return def_val;
    }
}

static void apic_disable_irq(uint8_t irq) {
    uint32_t reg = apic_read_ioapic(0x10 + irq * 2);
    reg |= APIC_REDIR_MASK;
    apic_write_ioapic(0x10 + irq * 2, reg);
}

int moe_install_irq(uint8_t irq, MOE_IRQ_HANDLER handler) {
    apic_madt_ovr_t ovr = get_madt_ovr(irq);
    irq_handler[ovr.gsi] = handler;
    apic_set_io_redirect(ovr.gsi, IRQ_BASE + ovr.gsi, ovr.flags, 0, apic_ids[0]);
    return 0;
}

int moe_uninstall_irq(uint8_t irq) {
    apic_madt_ovr_t ovr = get_madt_ovr(irq);
    apic_set_io_redirect(ovr.gsi, 0, 0, 1, INVALID_CPUID);
    irq_handler[ovr.gsi] = NULL;
    return 0;
}

int moe_install_msi(MOE_IRQ_HANDLER handler) {
    uint8_t current_msi = atomic_load(&next_msi);
    while (current_msi < MAX_MSI) {
        uint8_t _next_msi = current_msi + 1;
        if (atomic_compare_exchange_weak(&next_msi, &current_msi, _next_msi)) {
            uint8_t irq = MAX_IRQ - _next_msi;
            irq_handler[irq] = handler;
            return irq - MAX_IRQ;
        } else {
            cpu_relax();
        }
    }
    return 0;
}

uint8_t moe_make_msi_data(int irq, int mode, uint64_t *addr, uint32_t *data) {
    uint8_t vec = IRQ_BASE + MAX_IRQ + irq;
    *addr = MSI_BASE | (apic_ids[0] << 12);
    *data = (mode << 12) | vec;
    return vec;
}

void _irq_main(uint8_t irq, void* p) {
    MOE_IRQ_HANDLER handler = irq_handler[irq];
    if (handler) {
        handler(irq);
        apic_end_of_irq(irq);
    } else {
        if (irq < MAX_IOAPIC_IRQ) {
            apic_disable_irq(irq);
        }
        x64_context_t regs;
        regs.err = ((IRQ_BASE + irq) << 3) | ERROR_IDT | ERROR_EXT;
        regs.intnum = IRQ_BASE + irq;
        default_int_handler(&regs);
    }
}

void irq_livt() {
    lapic_timer_value++;
    apic_end_of_irq(0);
    thread_reschedule();
    if (smp_mode) {
        apic_write_lapic(0x300, 0xC0000 + IRQ_SCHEDULE);
    }
}

moe_measure_t moe_create_measure(int64_t us) {
    if (us == MOE_FOREVER) {
        return MOE_FOREVER;
    }
    if (us >= 0) {
        return lapic_timer_value + us / 1000;
    } else {
        return 0;
    }
}

int moe_measure_until(moe_measure_t deadline) {
    if (deadline == MOE_FOREVER) {
        return 1;
    }
    return (intptr_t)(deadline - lapic_timer_value) > 0;
}

int64_t moe_measure_diff(moe_measure_t from) {
    return (atomic_load(&lapic_timer_value) - from) * 1000;
}

int smp_send_invalidate_tlb() {
    // if (smp_mode) {
    //     apic_write_lapic(0x300, 0xC0000 + IRQ_INVALIDATE_TLB);
    //     moe_usleep(10000);
    // }
    return 0;
}

void ipi_invtlb_main(uintptr_t cr3) {
    apic_end_of_irq(0);
}

void ipi_sche_main() {
    apic_end_of_irq(0);
    thread_reschedule();
}

apic_id_t apic_read_apicid() {
    return apic_read_lapic(0x020) >> 24;
}

uintptr_t smp_get_current_cpuid_rdtscp() {
    uint32_t ecx;
    __asm__ volatile("rdtscp": "=c"(ecx));
    return ecx;
}

uintptr_t smp_get_current_cpuid_apic() {
    uint32_t apicid = apic_read_apicid();
    return apicid_to_cpuids[apicid];
}

uintptr_t smp_get_current_cpuid() {
    return smp_get_current_cpuid_apic();
}


// Initialize Application Processor (SMP)
void smp_init_ap(uint8_t cpuid) {
    gdt_setup();

    io_set_lazy_fpu_restore();

    tuple_eax_edx_t tuple = { cpuid };
    io_wrmsr(IA32_TSC_AUX_MSR, tuple);

    tuple_eax_edx_t msr_lapic = io_rdmsr(IA32_APIC_BASE_MSR);
    msr_lapic.u64 |= IA32_APIC_BASE_MSR_ENABLE;
    io_wrmsr(IA32_APIC_BASE_MSR, msr_lapic);

    apic_id_t apicid = apic_read_apicid();
    apicid_to_cpuids[apicid] = cpuid;

    apic_write_lapic(0x0F0, 0x10F);

    apic_write_lapic(0x3E0, 0x0000000B);
    apic_write_lapic(0x320, 0x00030000 | IRQ_LAPIC_TIMER);
    apic_write_lapic(0x380, lapic_timer_div);
}


static void apic_init() {
    acpi_madt_t* madt = acpi_find_table(ACPI_MADT_SIGNATURE);
    if (madt) {

        //  Disable Legacy PIC
        if (madt->Flags & ACPI_MADT_PCAT_COMPAT) {
            io_out8(0xA1, 0xFF);
            io_out8(0x21, 0xFF);
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
        pg_map_mmio(lapic_base, 1);

        apic_ids[n_cpu++] = apic_read_apicid();

        //  Parse structures
        size_t max_length = madt->Header.length - 44;
        uint8_t* p = madt->Structure;
        for (size_t loc = 0; loc < max_length; ) {
            size_t len = p[loc+1];
            void* madt_structure = (void*)(p+loc+2);

            // printf("%04x", (int)(intptr_t)loc);
            // for (int i = 0; i < len; i++) {
            //     printf(" %02x", p[loc + i]);
            // }
            // printf("\n");

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
                    ioapic_base = pg_map_mmio(ioapic->ioapic_base, 1);
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
        for (int i = 0; i < MAX_IOAPIC_IRQ; i++) {
            apic_disable_irq(i);
        }

        //  Install IDT handler
        idt_set_kernel_handler(IRQ_BASE + 0, (uintptr_t)&_irq0, 0);
        idt_set_kernel_handler(IRQ_BASE + 1, (uintptr_t)&_irq1, 0);
        idt_set_kernel_handler(IRQ_BASE + 2, (uintptr_t)&_irq2, 0);
        idt_set_kernel_handler(IRQ_BASE + 3, (uintptr_t)&_irq3, 0);
        idt_set_kernel_handler(IRQ_BASE + 4, (uintptr_t)&_irq4, 0);
        idt_set_kernel_handler(IRQ_BASE + 5, (uintptr_t)&_irq5, 0);
        idt_set_kernel_handler(IRQ_BASE + 6, (uintptr_t)&_irq6, 0);
        idt_set_kernel_handler(IRQ_BASE + 7, (uintptr_t)&_irq7, 0);
        idt_set_kernel_handler(IRQ_BASE + 8, (uintptr_t)&_irq8, 0);
        idt_set_kernel_handler(IRQ_BASE + 9, (uintptr_t)&_irq9, 0);
        idt_set_kernel_handler(IRQ_BASE + 10, (uintptr_t)&_irq10, 0);
        idt_set_kernel_handler(IRQ_BASE + 11, (uintptr_t)&_irq11, 0);
        idt_set_kernel_handler(IRQ_BASE + 12, (uintptr_t)&_irq12, 0);
        idt_set_kernel_handler(IRQ_BASE + 13, (uintptr_t)&_irq13, 0);
        idt_set_kernel_handler(IRQ_BASE + 14, (uintptr_t)&_irq14, 0);
        idt_set_kernel_handler(IRQ_BASE + 15, (uintptr_t)&_irq15, 0);
        idt_set_kernel_handler(IRQ_BASE + 16, (uintptr_t)&_irq16, 0);
        idt_set_kernel_handler(IRQ_BASE + 17, (uintptr_t)&_irq17, 0);
        idt_set_kernel_handler(IRQ_BASE + 18, (uintptr_t)&_irq18, 0);
        idt_set_kernel_handler(IRQ_BASE + 19, (uintptr_t)&_irq19, 0);
        idt_set_kernel_handler(IRQ_BASE + 20, (uintptr_t)&_irq20, 0);
        idt_set_kernel_handler(IRQ_BASE + 21, (uintptr_t)&_irq21, 0);
        idt_set_kernel_handler(IRQ_BASE + 22, (uintptr_t)&_irq22, 0);
        idt_set_kernel_handler(IRQ_BASE + 23, (uintptr_t)&_irq23, 0);
        idt_set_kernel_handler(IRQ_BASE + 24, (uintptr_t)&_irq24, 0);
        idt_set_kernel_handler(IRQ_BASE + 25, (uintptr_t)&_irq25, 0);
        idt_set_kernel_handler(IRQ_BASE + 26, (uintptr_t)&_irq26, 0);
        idt_set_kernel_handler(IRQ_BASE + 27, (uintptr_t)&_irq27, 0);
        idt_set_kernel_handler(IRQ_BASE + 28, (uintptr_t)&_irq28, 0);
        idt_set_kernel_handler(IRQ_BASE + 29, (uintptr_t)&_irq29, 0);
        idt_set_kernel_handler(IRQ_BASE + 30, (uintptr_t)&_irq30, 0);
        idt_set_kernel_handler(IRQ_BASE + 31, (uintptr_t)&_irq31, 0);
        idt_set_kernel_handler(IRQ_BASE + 32, (uintptr_t)&_irq32, 0);
        idt_set_kernel_handler(IRQ_BASE + 33, (uintptr_t)&_irq33, 0);
        idt_set_kernel_handler(IRQ_BASE + 34, (uintptr_t)&_irq34, 0);
        idt_set_kernel_handler(IRQ_BASE + 35, (uintptr_t)&_irq35, 0);
        idt_set_kernel_handler(IRQ_BASE + 36, (uintptr_t)&_irq36, 0);
        idt_set_kernel_handler(IRQ_BASE + 37, (uintptr_t)&_irq37, 0);
        idt_set_kernel_handler(IRQ_BASE + 38, (uintptr_t)&_irq38, 0);
        idt_set_kernel_handler(IRQ_BASE + 39, (uintptr_t)&_irq39, 0);
        idt_set_kernel_handler(IRQ_BASE + 40, (uintptr_t)&_irq40, 0);
        idt_set_kernel_handler(IRQ_BASE + 41, (uintptr_t)&_irq41, 0);
        idt_set_kernel_handler(IRQ_BASE + 42, (uintptr_t)&_irq42, 0);
        idt_set_kernel_handler(IRQ_BASE + 43, (uintptr_t)&_irq43, 0);
        idt_set_kernel_handler(IRQ_BASE + 44, (uintptr_t)&_irq44, 0);
        idt_set_kernel_handler(IRQ_BASE + 45, (uintptr_t)&_irq45, 0);
        idt_set_kernel_handler(IRQ_BASE + 46, (uintptr_t)&_irq46, 0);
        idt_set_kernel_handler(IRQ_BASE + 47, (uintptr_t)&_irq47, 0);

        idt_set_kernel_handler(IRQ_SCHEDULE, (uintptr_t)&_ipi_sche, 0);
        // idt_set_kernel_handler(IRQ_INVALIDATE_TLB, (uintptr_t)&_ipi_invtlb, 0);

        // LAPIC timer
        apic_write_lapic(0x3E0, 0x0000000B);
        apic_write_lapic(0x320, 0x00010020);
        apic_write_lapic(0x380, UINT32_MAX);
        const int magic_number = 100;
        uint32_t timer_val = ACPI_PM_TIMER_FREQ / magic_number;
        uint32_t acpi_tmr_base = acpi_read_pm_timer();
        uint32_t acpi_tmr_val = acpi_read_pm_timer();
        do {
            cpu_relax();
        } while (acpi_tmr_val == acpi_read_pm_timer());
        do {
            cpu_relax();
            acpi_tmr_val = acpi_read_pm_timer();
        } while (timer_val > ((acpi_tmr_val - acpi_tmr_base) & 0xFFFFFF));
        uint32_t count = apic_read_lapic(0x390);
        uint64_t lapic_freq = ((uint64_t)UINT32_MAX - count) * magic_number;

        lapic_timer_div = lapic_freq / 1000;
        lapic_timer_div2 = lapic_freq / 1000000;

        irq_handler[0] = &irq_livt;
        apic_write_lapic(0x320, 0x00020000 | IRQ_LAPIC_TIMER);
        apic_write_lapic(0x380, lapic_timer_div);

        // Then enable IRQ
        __asm__ volatile("sti");

        // Initialize SMP
        if (n_cpu > 1) {
            uint8_t vector_sipi = moe_alloc_gates_memory() >> 12;
            int max_cpu = MIN(n_cpu, MAX_CPU);
            const uintptr_t stack_chunk_size = 0x4000;
            uintptr_t* stacks = moe_alloc_object(stack_chunk_size, max_cpu);
            _Atomic uint32_t* wait_p = smp_setup_init(vector_sipi, max_cpu, stack_chunk_size, stacks);
            apic_write_lapic(0x300, 0x000C4500);
            moe_usleep(10000);
            apic_write_lapic(0x300, 0x000C4600 + vector_sipi);
            moe_usleep(200000);
            thread_init(atomic_load(wait_p));
            smp_mode = 1;
        } else {
            thread_init(1);
        }
    }
}


/*********************************************************************/


void _int07_main() {
    // TODO:
}


/*********************************************************************/
//  Peripheral Component Interconnect

#define PCI_CONFIG_ADDRESS  0x0CF8
#define PCI_CONFIG_DATA     0x0CFC
#define PCI_ADDRESS_ENABLE  0x80000000

uint32_t pci_make_reg_addr(uint8_t bus, uint8_t dev, uint8_t func, uintptr_t reg) {
    return (reg & 0xFC) | ((func) << 8) | ((dev) << 11) | (bus << 16) | ((reg & 0xF00) << 16);
}

static uint32_t _pci_read_config(uint32_t addr) {
    io_out32(PCI_CONFIG_ADDRESS, PCI_ADDRESS_ENABLE | addr);
    return io_in32(PCI_CONFIG_DATA);
}

static void _pci_write_config(uint32_t addr, uint32_t val) {
    io_out32(PCI_CONFIG_ADDRESS, PCI_ADDRESS_ENABLE | addr);
    io_out32(PCI_CONFIG_DATA, val);
}

static void _pci_disable_config() {
    io_out32(PCI_CONFIG_ADDRESS, 0);
}

int pci_parse_bar(uint32_t _base, unsigned idx, uint64_t *_bar, uint64_t *_size) {

    if (idx >= 6) return 0;

    uint32_t base = (_base & ~0xFF) + 0x10 + idx * 4;
    uint64_t bar0 = _pci_read_config(base);
    uint64_t nbar;
    int is_bar64 = ((bar0 & 7) == 0x04);

    if (!_size) {
        if (is_bar64) {
            uint32_t bar1 = _pci_read_config(base + 4);
            bar0 |= (uint64_t)bar1 << 32;
        }
        *_bar = bar0;
        return is_bar64 ? 2 : 1;
    }

    if (is_bar64) {
        uint32_t bar1 = _pci_read_config(base + 4);
        _pci_write_config(base, UINT32_MAX);
        _pci_write_config(base + 4, UINT32_MAX);
        nbar = _pci_read_config(base) | ((uint64_t)_pci_read_config(base + 4) << 32);
        _pci_write_config(base, bar0);
        _pci_write_config(base + 4, bar1);
        bar0 |= (uint64_t)bar1 << 32;
    } else {
        _pci_write_config(base, UINT32_MAX);
        nbar = _pci_read_config(base);
        _pci_write_config(base, bar0);
    }
    _pci_disable_config();
    if (nbar) {
        if (is_bar64) {
            nbar ^= UINT64_MAX;
        } else {
            nbar ^= UINT32_MAX;
        }
        if (bar0 & 1) {
            nbar = (nbar | 3) & UINT16_MAX;
        } else {
            nbar |= 15;
        }
        *_bar = bar0;
        *_size = nbar;
        return is_bar64 ? 2 : 1;
    } else {
        return 0;
    }
}


uint32_t pci_find_by_class(uint32_t cls, uint32_t mask) {
    int bus = 0;
    for (int dev = 0; dev < 32; dev++) {
        uint32_t data = _pci_read_config(pci_make_reg_addr(bus, dev, 0, 0));
        if ((data & 0xFFFF) == 0xFFFF) continue;
            uint32_t PCI0C = _pci_read_config(pci_make_reg_addr(bus, dev, 0, 0x0C));
            int limit = (PCI0C & 0x00800000) ? 8 : 1;
            for (int func = 0; func < limit; func++) {
                uint32_t base = pci_make_reg_addr(bus, dev, func, 0);
                uint32_t data = pci_read_config(base);
                if ((data & 0xFFFF) != 0xFFFF) {
                    uint32_t PCI08 = pci_read_config(base + 0x08);
                    if ((PCI08 & mask) == cls) {
                        _pci_disable_config();
                        return PCI_ADDRESS_ENABLE | base;
                    }
                }
            }
    }
    _pci_disable_config();
    return 0;
}

uint32_t pci_find_capability(uint32_t base, uint8_t id) {
    uint8_t cap_ptr = _pci_read_config(base + 0x34) & 0xFF;
    while (cap_ptr) {
        uint32_t data = _pci_read_config(base + cap_ptr);
        if ((data & 0xFF) == id) {
            _pci_disable_config();
            return cap_ptr;
        }
        cap_ptr = (data >> 8) & 0xFF;
    }
    _pci_disable_config();
    return 0;
}

void pci_dump_config(uint32_t base, void *p) {
    uint32_t *d = (uint32_t *)p;
    for (uint32_t i = 0; i < 256 / 4; i++) {
        d[i] = _pci_read_config(base + i * 4);
    }
    _pci_disable_config();
}


uint32_t pci_read_config(uint32_t addr) {
    uint32_t retval = _pci_read_config(addr);
    _pci_disable_config();
    return retval;
}

void pci_write_config(uint32_t addr, uint32_t val) {
    _pci_write_config(addr, val);
    _pci_disable_config();
}


static void pci_init() {
    // do nothing
}


/*********************************************************************/

_Noreturn void arch_reset() {
    io_out8(0x0CF9, 0x06);
    moe_usleep(10000);
    io_out8(0x0092, 0x01);
    moe_usleep(10000);
    for (;;) { io_hlt(); }
}

void arch_init(moe_bootinfo_t* info) {

    tuple_eax_edx_t tuple = { 0 };
    io_wrmsr(IA32_TSC_AUX_MSR, tuple);
    cs_sel = gdt_init();
    gdt_setup();
    idt_init();

    pci_init();
    apic_init();
    acpi_enable(1);

    lpc_init();

    // cpuid_t regs = {0x80000001};
    // io_cpuid(&regs);
    // printf("CPUID %08x %08x %08x %08x\n", regs.eax, regs.ecx, regs.edx, regs.ebx);
}

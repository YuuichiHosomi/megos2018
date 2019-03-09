// Minimal ACPI Support
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "kernel.h"
#include "acpi.h"


static acpi_rsd_ptr_t* rsdp = NULL;
static acpi_xsdt_t* xsdt = NULL;
static acpi_fadt_t* fadt = NULL;
static acpi_dsdt_t* dsdt = NULL;
static intptr_t n_entries_xsdt = 0;
static uint8_t SLP_TYP5a = 0;
static uint8_t SLP_TYP5b = 0;


static void acpi_outb(uintptr_t port, uint8_t value) {
    __asm__ volatile ("outb %%al, %%dx":: "a"(value), "d"(port));
}

static void acpi_outw(uintptr_t port, uint16_t value) {
    __asm__ volatile ("outw %%ax, %%dx":: "a"(value), "d"(port));
}

static void acpi_outl(uintptr_t port, uint32_t value) {
    __asm__ volatile ("outl %%eax, %%dx":: "a"(value), "d"(port));
}

static uint8_t acpi_inb(uintptr_t port) {
    uint16_t value;
    __asm__ volatile ("inb %%dx, %%al": "=a"(value) :"d"(port));
    return value;
}

static uint16_t acpi_inw(uintptr_t port) {
    uint16_t value;
    __asm__ volatile ("inw %%dx, %%ax": "=a"(value) :"d"(port));
    return value;
}

static uint32_t acpi_inl(uintptr_t port) {
    uint32_t value;
    __asm__ volatile ("inl %%dx, %%eax": "=a"(value) :"d"(port));
    return value;
}


// Generic Address Structure
void acpi_gas_output(acpi_gas_t *_gas, uintptr_t value) {
    acpi_gas_t gas = *_gas;
    MOE_ASSERT(gas.bit_offset == 0, "Cannot decode GAS");
    MOE_ASSERT(gas.bit_width, "UNKNOWN BIT WIDTH");
    switch(gas.address_space_id) {
        case 0: // MEMORY
            switch(gas.bit_width) {
                case 8:
                    WRITE_PHYSICAL_UINT8(gas.address, value);
                    break;
                case 32:
                    WRITE_PHYSICAL_UINT32(gas.address, value);
                    break;
                default:
                    MOE_ASSERT(false, "Cannot access GAS");
            }
            break;
        case 1: // I/O
            switch(gas.bit_width) {
                case 8:
                    acpi_outb(gas.address, value);
                    break;
                case 16:
                    acpi_outw(gas.address, value);
                    break;
                case 32:
                    acpi_outl(gas.address, value);
                    break;
            }
            break;
        default:
            MOE_ASSERT(false, "UNKNOWN GAS");
    }

}

static int is_equal_signature(const void* p1, const void* p2) {
    const uint32_t* _p1 = (const uint32_t*)p1;
    const uint32_t* _p2 = (const uint32_t*)p2;
    return (*_p1 == *_p2);
}

int acpi_get_number_of_table_entries() {
    return n_entries_xsdt;
}

void* acpi_enum_table_entry(int index) {
    if (!xsdt) return NULL;
    if (index >= n_entries_xsdt) return NULL;
    return PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(xsdt->Entry[index]);
}

void* acpi_find_table(const char* signature) {
    if (!xsdt) return NULL;
    for (int i = 0; i < n_entries_xsdt; i++) {
        acpi_header_t* entry = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(xsdt->Entry[i]);
        if (is_equal_signature(entry->signature, signature)) {
            return entry;
        }
    }
    return NULL;
}

int acpi_enable(int enabled) {
    if (fadt->Flags & ACPI_FADT_HW_REDUCED_ACPI) {
        return enabled;
    }
    uint8_t ACPI_ENABLE = fadt->ACPI_ENABLE;
    uint32_t SMI_CMD = fadt->SMI_CMD, PM1a_CNT = fadt->PM1a_CNT_BLK;
    if (SMI_CMD == 0 || PM1a_CNT == 0 || ACPI_ENABLE == 0) {
        return 0;
    }
    if (enabled) {
        uint32_t ax = acpi_inw(PM1a_CNT);
        if ((ax & ACPI_PM1_SCI_EN) != 0) {
            return 1;
        }
        acpi_outb(SMI_CMD, ACPI_ENABLE);
        moe_timer_t timer = moe_create_interval_timer(3000000);
        do {
            ax = acpi_inw(PM1a_CNT);
            if ((ax & ACPI_PM1_SCI_EN) != 0) {
                break;
            }
            moe_usleep(10000);
        } while (moe_check_timer(&timer));
        uint32_t PM1b_CNT = fadt->PM1b_CNT_BLK;
        if (PM1b_CNT != 0) {
            moe_timer_t timer = moe_create_interval_timer(3000000);
            do {
                ax = acpi_inw(PM1b_CNT);
                if ((ax & ACPI_PM1_SCI_EN) != 0) {
                    break;
                }
                moe_usleep(10000);
            } while (moe_check_timer(&timer));
        }
        return ((ax & ACPI_PM1_SCI_EN) != 0);
    } else {
        return 0;
    }
}

// Reset if possible
void acpi_reset() {
    acpi_enable(1);
    if (fadt->Flags & ACPI_FADT_RESET_REG_SUP) {
        acpi_gas_output(&fadt->RESET_REG, fadt->RESET_VALUE);
        for(;;) { io_hlt(); }
    }
}

// ACPI Sx
void acpi_set_sleep(int state) {
    if (state != 5) return; // I want S5 only
    if (!acpi_enable(1)) return;
    if (fadt->Flags & ACPI_FADT_HW_REDUCED_ACPI) {
        acpi_gas_output(&fadt->SLEEP_CONTROL_REG, ACPI_SCR_SLP_EN | (SLP_TYP5a << 2));
    } else {
        acpi_outw(fadt->PM1a_CNT_BLK, ACPI_PM1_SLP_EN | (SLP_TYP5a << 10));
        if (fadt->PM1b_CNT_BLK) {
            acpi_outw(fadt->PM1b_CNT_BLK, ACPI_PM1_SLP_EN | (SLP_TYP5b << 10));
        }
    }
    for(;;) { io_hlt(); }
}

void acpi_init(acpi_rsd_ptr_t* _rsdp) {
    rsdp = _rsdp;
    xsdt = (acpi_xsdt_t*)PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(rsdp->xsdtaddr);
    n_entries_xsdt = (xsdt->Header.length - 0x24 /* offset_of Entry */ ) / sizeof(xsdt->Entry[0]);

    fadt = acpi_enum_table_entry(0);
    MOE_ASSERT(fadt, "FADT NOT FOUND");

    dsdt = (void*)(uintptr_t)fadt->DSDT;
    if (dsdt == NULL) {
        dsdt = (void*)(uintptr_t)fadt->X_DSDT;
    }
    MOE_ASSERT(dsdt, "DSDT NOT FOUND");

    // Search S5
    {
        uint8_t *p = (uint8_t*)&dsdt->Structure;
        size_t maxlen = (dsdt->Header.length - 0x24);
        for (size_t i = 0; i < maxlen; i++) {
            // NameOp(08) '_S5_' PackageOp(12)
            if (!is_equal_signature(p+i, "_S5_")) continue;
            if ((p[i+4] != 0x12) || ((p[i-1] != 0x08) && (p[i-2] != 0x08 || p[i-1] != '\\'))) continue;

            i += 5;
            i += ((p[i] & 0xC0) >> 6) + 2;

            if (p[i] == 0x0A) i++;
            SLP_TYP5a = 0x07 & p[i++];

            if (p[i] == 0x0A) i++;
            SLP_TYP5b = 0x07 & p[i++];

            break;
        }
    }
}

int irq_sci(int irq) {

    printf("ACPI_SCI: ");

    if (fadt->PM1a_EVT_BLK) {
        uint16_t eax = 0;
        uint32_t edx = fadt->PM1a_EVT_BLK;
        acpi_outw(edx, eax = acpi_inw(edx));
        printf("a: %04x", eax);
    }

    if (fadt->PM1b_EVT_BLK) {
        uint16_t eax = 0;
        uint32_t edx = fadt->PM1b_EVT_BLK;
        acpi_outw(edx, eax = acpi_inw(edx));
        printf("b: %04x", eax);
    }

    if (fadt->GPE0_BLK) {
        uintptr_t gpe_blk = fadt->GPE0_BLK;
        uintptr_t gpe_len = fadt->GPE0_BLK_LEN / 2;
        for (int i = 0; i < gpe_len; i++) {
            uint8_t al;
            uint32_t edx = gpe_blk + i;
            acpi_outb(edx, al = acpi_inb(edx));
            printf(" %02x", al);
        }
    }

    printf("\n");
    return 0;
}

void acpi_init_sci() {

    return;

    if (fadt->SCI_INT) {
        moe_enable_irq(fadt->SCI_INT, irq_sci);
    }

    acpi_enable(1);

        // enable POWER BUTTON
        if (fadt->PM1a_EVT_BLK) {
            uintptr_t pm_evt_blk = fadt->PM1a_EVT_BLK;
            uintptr_t pm_evt_len = fadt->PM1_EVT_LEN / 2;
            uint32_t eax = 0x0300;
            __asm__ volatile ("outw %%ax, %%dx" :: "a"(eax), "d"(pm_evt_blk));
            __asm__ volatile ("outw %%ax, %%dx" :: "a"(eax), "d"(pm_evt_blk + pm_evt_len));
        }

        // disable pm1b event
        if (fadt->PM1b_EVT_BLK) {
            uintptr_t pm_evt_blk = fadt->PM1b_EVT_BLK;
            uintptr_t pm_evt_len = fadt->PM1_EVT_LEN / 2;
            uint32_t eax = 0x0300;
            __asm__ volatile ("outw %%ax, %%dx" :: "a"(eax), "d"(pm_evt_blk));
            __asm__ volatile ("outw %%ax, %%dx" :: "a"(eax), "d"(pm_evt_blk + pm_evt_len));
        }

    // DISABLE GPE
    // if (fadt->GPE0_BLK) {
    //     uintptr_t gpe_blk = fadt->GPE0_BLK;
    //     uintptr_t gpe_len = fadt->GPE0_BLK_LEN / 2;
    //     for (int i = 0; i < gpe_len; i++) {
    //         __asm__ volatile ("outb %%al, %%dx" :: "a"(0x00), "d"(gpe_blk + i));
    //         __asm__ volatile ("outb %%al, %%dx" :: "a"(0x00), "d"(gpe_blk + i + gpe_len));
    //     }
    // }

}

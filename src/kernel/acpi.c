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
                case 16:
                    WRITE_PHYSICAL_UINT16(gas.address, value);
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
                    io_out8(gas.address, value);
                    break;
                case 16:
                    io_out16(gas.address, value);
                    break;
                case 32:
                    io_out32(gas.address, value);
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
    return MOE_PA2VA(xsdt->Entry[index]);
}

void* acpi_find_table(const char* signature) {
    if (!xsdt) return NULL;
    for (int i = 0; i < n_entries_xsdt; i++) {
        acpi_header_t* entry = MOE_PA2VA(xsdt->Entry[i]);
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
        uint32_t ax = io_in16(PM1a_CNT);
        if ((ax & ACPI_PM1_SCI_EN) != 0) {
            return 1;
        }
        io_out8(SMI_CMD, ACPI_ENABLE);
        moe_timer_t timer = moe_create_interval_timer(3000000);
        do {
            ax = io_in16(PM1a_CNT);
            if ((ax & ACPI_PM1_SCI_EN) != 0) {
                break;
            }
            moe_usleep(10000);
        } while (moe_check_timer(&timer));
        uint32_t PM1b_CNT = fadt->PM1b_CNT_BLK;
        if (PM1b_CNT != 0) {
            moe_timer_t timer = moe_create_interval_timer(3000000);
            do {
                ax = io_in16(PM1b_CNT);
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

// Enter ACPI Sleeping State Sx
void acpi_enter_sleep_state(int state) {
    if (state != 5) return; // I want S5 only
    if (!acpi_enable(1)) return;
    if (fadt->Flags & ACPI_FADT_HW_REDUCED_ACPI) {
        acpi_gas_output(&fadt->SLEEP_CONTROL_REG, ACPI_SCR_SLP_EN | (SLP_TYP5a << 2));
    } else {
        io_out16(fadt->PM1a_CNT_BLK, ACPI_PM1_SLP_EN | (SLP_TYP5a << 10));
        if (fadt->PM1b_CNT_BLK) {
            io_out16(fadt->PM1b_CNT_BLK, ACPI_PM1_SLP_EN | (SLP_TYP5b << 10));
        }
    }
    for(;;) { io_hlt(); }
}

void acpi_init(acpi_rsd_ptr_t* _rsdp) {
    rsdp = _rsdp;
    xsdt = (acpi_xsdt_t*)MOE_PA2VA(rsdp->xsdtaddr);
    n_entries_xsdt = (xsdt->Header.length - 0x24 /* offset_of Entry */ ) / sizeof(xsdt->Entry[0]);

    fadt = mm_alloc_static_page(sizeof(acpi_fadt_t));
    MOE_ASSERT(fadt, "FADT NOT FOUND");
    acpi_fadt_t *p = acpi_find_table(ACPI_FADT_SIGNATURE);
    MOE_ASSERT(p, "FADT NOT FOUND");
    memset(fadt, 0, sizeof(acpi_fadt_t));
    memcpy(fadt, p, MIN(p->Header.length, sizeof(acpi_fadt_t)));

    dsdt = (void*)(uintptr_t)fadt->X_DSDT;
    if (dsdt == NULL) {
        dsdt = (void*)(uintptr_t)fadt->DSDT;
    }
    MOE_ASSERT(dsdt, "DSDT NOT FOUND");

    // Search S5
    {
        uint8_t *p = (uint8_t*)&dsdt->Structure;
        size_t maxlen = (dsdt->Header.length - 0x24);
        for (size_t i = 0; i < maxlen;) {
            // NameOp(08) '_S5_' PackageOp(12)
            if (p[i++] != 0x08) continue;
            if (p[i] == '\\') i++;
            if (!is_equal_signature(p + i, "_S5_")) continue;
            if (p[i+4] != 0x12) continue;
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

void irq_sci(int irq) {

    uint16_t pm1_evt = 0;

    if (fadt->PM1a_EVT_BLK) {
        uint16_t ax;
        uint32_t edx = fadt->PM1a_EVT_BLK;
        io_out16(edx, ax = io_in16(edx));
        pm1_evt |= ax;
    }

    if (fadt->PM1b_EVT_BLK) {
        uint16_t ax;
        uint32_t edx = fadt->PM1b_EVT_BLK;
        io_out16(edx, ax = io_in16(edx));
        pm1_evt |= ax;
    }

    printf("ACPI_SCI: %04x", pm1_evt);

    // Power button
    if (pm1_evt & 0x0100) {
        moe_shutdown_system();
    }

    if (fadt->GPE0_BLK) {
        uintptr_t gpe_blk = fadt->GPE0_BLK;
        uintptr_t gpe_len = fadt->GPE0_BLK_LEN / 2;
        for (int i = 0; i < gpe_len; i++) {
            uint8_t al;
            uint32_t edx = gpe_blk + i;
            io_out8(edx, al = io_in8(edx));
            printf(" %02x", al);
        }
    }

    printf("\n");
}


void acpi_init_sci() {

    if (fadt->SCI_INT) {
        moe_enable_irq(fadt->SCI_INT, irq_sci);
    }

    // printf("ACPI %08llx %08x %02x %04x %02x %04x %02x %04x\n", (uintptr_t)fadt, fadt->Flags, fadt->SCI_INT, fadt->SMI_CMD, fadt->ACPI_ENABLE, fadt->PM1a_CNT_BLK, SLP_TYP5a, (uint16_t)fadt->SLEEP_CONTROL_REG.address);
    // for (int i = 0; i < 15; i++) {
    //     moe_usleep(1000000);
    //     printf(".");
    // }
    // acpi_set_sleep(5);

    acpi_enable(1);

    // enable power button
    if (fadt->PM1a_EVT_BLK) {
        uint32_t edx = fadt->PM1a_EVT_BLK;
        uint32_t len = fadt->PM1_EVT_LEN / 2;
        io_out16(edx + len, 0x0300);
    }

    if (fadt->PM1b_EVT_BLK) {
        uint32_t edx = fadt->PM1b_EVT_BLK;
        uint32_t len = fadt->PM1_EVT_LEN / 2;
        io_out16(edx + len, 0x0300);
    }

    //  disable all GPE
    if (fadt->GPE0_BLK) {
        uintptr_t gpe_blk = fadt->GPE0_BLK;
        uintptr_t gpe_len = fadt->GPE0_BLK_LEN / 2;
        for (int i = 0; i < gpe_len; i++) {
            io_out8(gpe_blk + gpe_len + i, 0);
        }
    }

}

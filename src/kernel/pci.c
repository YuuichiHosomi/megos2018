//  Peripheral Component Interconnect
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include "moe.h"
#include "kernel.h"

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
                        return PCI_ADDRESS_ENABLE | base;
                    }
                }
            }
    }
    return 0;
}

uint32_t pci_find_capability(uint32_t base, uint8_t id) {
    uint8_t cap_ptr = _pci_read_config(base + 0x34) & 0xFF;
    while (cap_ptr) {
        uint32_t data = _pci_read_config(base + cap_ptr);
        if ((data & 0xFF) == id) {
            return cap_ptr;
        }
        cap_ptr = (data >> 8) & 0xFF;
    }
    return 0;
}

void pci_dump_config(uint32_t base, void *p) {
    uint32_t *d = (uint32_t *)p;
    for (uint32_t i = 0; i < 256 / 4; i++) {
        d[i] = _pci_read_config(base + i * 4);
    }
}


uint32_t pci_read_config(uint32_t addr) {
    uint32_t retval = _pci_read_config(addr);
    return retval;
}

void pci_write_config(uint32_t addr, uint32_t val) {
    _pci_write_config(addr, val);
}


void pci_init(void) {
    // do nothing
}


int cmd_lspci(int argc, char **argv) {
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
                uint16_t vid = data & 0xFFFF;
                uint16_t did = data >> 16;
                uint32_t cls = PCI08 >> 8;
                printf("%02d:%02d.%d %04x:%04x %06x\n", bus, dev, func, vid, did, cls);
            }
        }
    }
    return 0;
}



/*

    Minimal ACPI Support

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
#include "acpi.h"


static acpi_rsd_ptr_t* rsdp = NULL;
static acpi_xsdt_t* xsdt = NULL;
static int n_entries_xsdt = 0;


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

void acpi_init(acpi_rsd_ptr_t* _rsdp) {
    rsdp = _rsdp;
    xsdt = (acpi_xsdt_t*)PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(rsdp->xsdtaddr);
    n_entries_xsdt = (xsdt->Header.length - 0x24 /* offset_of Entry */ ) / sizeof(xsdt->Entry[0]);
}

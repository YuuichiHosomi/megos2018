// Minimal PE Loader
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "boot.h"
#include "pe.h"

uintptr_t pe_offset_0;
pe64_header_t* pe_hdr;
pe_section_table_t *sec_tbl;

uint64_t pe64_locate(uint64_t base);

IMAGE_LOCATOR recognize_kernel_signature(struct iovec obj) {
    pe_offset_0 = (uintptr_t)obj.iov_base;

    uint16_t *mz = (uint16_t*)pe_offset_0;
    if (mz[0] != IMAGE_DOS_SIGNATURE) return NULL;

    uint32_t ne_ptr = *((uint32_t *)(pe_offset_0 + 0x3C));
    pe_hdr = (pe64_header_t *)(pe_offset_0 + ne_ptr);
    if (pe_hdr->pe_signature != IMAGE_NT_SIGNATURE
        || pe_hdr->coff_header.machine != IMAGE_FILE_MACHINE_AMD64
        || (pe_hdr->coff_header.coff_flags & IMAGE_FILE_EXECUTABLE_IMAGE) == 0
        || pe_hdr->optional_header.magic != MAGIC_PE64
        ) return NULL;

    sec_tbl = (pe_section_table_t *)((uintptr_t)pe_hdr + 4 + sizeof(pe_coff_header_t) + pe_hdr->coff_header.size_of_optional);

    return &pe64_locate;
}

uint64_t pe64_locate(uint64_t base) {

    uint64_t image_base = pe_hdr->optional_header.image_base;

    // step1 allocate memory
    uint8_t *vmem = valloc(base, pe_hdr->optional_header.size_of_image);
    memset(vmem, 0, pe_hdr->optional_header.size_of_image);

    // step2 locate sections
    for (int i = 0; i < pe_hdr->coff_header.n_sections; i++) {
        pe_section_table_t *sec = sec_tbl + i;
        if (sec->size) {
            void *p = vmem + sec->rva;
            void *q = (void*)(pe_offset_0 + sec->file_offset);
            size_t z = MIN(sec->vsize, sec->size);
            memcpy(p, q, z);
        }
    }

    // step3 relocate
    size_t reloc_size = pe_hdr->dir[IMAGE_DIRECTORY_ENTRY_BASERELOC].size;
    uint8_t *reloc_base = vmem + pe_hdr->dir[IMAGE_DIRECTORY_ENTRY_BASERELOC].rva;
    for (uintptr_t i = 0; i < reloc_size; ) {
        pe_basereloc_t *reloc = (pe_basereloc_t *)(reloc_base + i);
        size_t count = (reloc->size - 8) / sizeof(uint16_t);
        for (int j = 0 ; j < count; j++) {
            uintptr_t rva = reloc->rva_base + reloc->entry[j].value;
            switch (reloc->entry[j].type) {
                case IMAGE_REL_BASED_ABSOLUTE:
                    break;
                case IMAGE_REL_BASED_DIR64:
                    {
                        uint64_t *p = (uint64_t *)(vmem + rva);
                        *p = *p - image_base + base;
                    }
                    break;
            }
        }
        i += reloc->size;
    }

    // step4 set attributes
    for (int i = 0; i < pe_hdr->coff_header.n_sections; i++) {
        pe_section_table_t *sec = sec_tbl + i;
        int attr = ((sec->flags & IMAGE_SCN_MEM_WRITE) ? PROT_WRITE : 0)
            | ((sec->flags & IMAGE_SCN_MEM_READ) ? PROT_READ : 0)
            | ((sec->flags & IMAGE_SCN_MEM_EXECUTE) ? PROT_EXEC : 0);
        vprotect(base + sec->rva, sec->vsize, attr);
    }

    return base + pe_hdr->optional_header.entry_point;
}

// Minimal OS EFI Stub
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "efi.h"

extern _Noreturn void start_kernel(moe_bootinfo_t* bootinfo);

#define	EFI_PRINT(s)	st->ConOut->OutputString(st->ConOut, L ## s)

CONST EFI_GUID EfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
CONST EFI_GUID efi_acpi_20_table_guid = EFI_ACPI_20_TABLE_GUID;

/*********************************************************************/

static inline int IsEqualGUID(CONST EFI_GUID* guid1, CONST EFI_GUID* guid2) {
    uint64_t* p = (uint64_t*)guid1;
    uint64_t* q = (uint64_t*)guid2;
    return (p[0] == q[0]) && (p[1] == q[1]);
}

static void* efi_find_config_table(EFI_SYSTEM_TABLE *st, CONST EFI_GUID* guid) {
    for (int i = 0; i < st->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE* tab = st->ConfigurationTable + i;
        if (IsEqualGUID(&tab->VendorGuid, guid)) {
            return tab->VendorTable;
        }
    }
    return NULL;
}

/*********************************************************************/

EFI_STATUS EFIAPI efi_main(IN EFI_HANDLE image, IN EFI_SYSTEM_TABLE *st) {
    EFI_BOOT_SERVICES* bs;
    EFI_STATUS status;
    moe_bootinfo_t bootinfo;

    // Init UEFI Environments
    bs = st->BootServices;

    //	Find ACPI table pointer
    {
        bootinfo.acpi = efi_find_config_table(st, &efi_acpi_20_table_guid);
        if (!bootinfo.acpi) {
            EFI_PRINT("ERROR: ACPI NOT FOUND");
            goto errexit;
        }
    }

    //	Get GOP
    {
        EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;

        status = bs->LocateProtocol(&EfiGraphicsOutputProtocolGuid, NULL, (void**)&gop);
        if (!gop) {
            EFI_PRINT("ERROR: GOP NOT FOUND");
            goto errexit;
        }

        bootinfo.video.vram = (void*)gop->Mode->FrameBufferBase;
        bootinfo.video.res_x = gop->Mode->Info->HorizontalResolution;
        bootinfo.video.res_y = gop->Mode->Info->VerticalResolution;
        bootinfo.video.pixel_per_scan_line = gop->Mode->Info->PixelsPerScanLine;
    }

    //	Exit BootServices
    {
        EFI_MEMORY_DESCRIPTOR* mmap = NULL;
        UINTN mmapsize = 0;
        UINTN mapkey, descriptorsize;
        UINT32 descriptorversion;

        do {
            status = bs->GetMemoryMap(&mmapsize, mmap, &mapkey, &descriptorsize, &descriptorversion);
            while (status == EFI_BUFFER_TOO_SMALL) {
                if (mmap) {
                    bs->FreePool(mmap);
                }
                status = bs->AllocatePool(EfiLoaderData, mmapsize, (void**)&mmap);
                status = bs->GetMemoryMap(&mmapsize, mmap, &mapkey, &descriptorsize, &descriptorversion);
            }
            status = bs->ExitBootServices(image, mapkey);
        } while (EFI_ERROR(status));

        bootinfo.mmap.mmap = mmap;
        bootinfo.mmap.size = mmapsize;
        bootinfo.mmap.desc_size = descriptorsize;
        bootinfo.mmap.desc_version = descriptorversion;
        bootinfo.efiRT = st->RuntimeServices;
    }

    //	Start Kernel
    start_kernel(&bootinfo);

errexit:
    return EFI_LOAD_ERROR;

}

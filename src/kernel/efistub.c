// Minimal OS EFI Stub
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"
#include "efi.h"

extern _Noreturn void start_kernel(moe_bootinfo_t* bootinfo);

#define	EFI_PRINT(s)	st->ConOut->OutputString(st->ConOut, L ## s)

/*********************************************************************/

moe_bootinfo_t bootinfo;

EFI_STATUS EFIAPI efi_main(IN EFI_HANDLE image, IN EFI_SYSTEM_TABLE *st) {

    if (st) {
        EFI_PRINT("This program is not valid UEFI app.\r\n");
        return EFI_LOAD_ERROR;
    } else {
        moe_bootinfo_t *non_efi_bootinfo = (moe_bootinfo_t *)image;
        bootinfo = *non_efi_bootinfo;
    }

    // Start Kernel
    start_kernel(&bootinfo);
}

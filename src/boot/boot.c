// MEG-OS Loader for EFI
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include "boot.h"
#include "efish.h"

#define	INVALID_UNICHAR	0xFFFE
#define	ZWNBSP	0xFEFF
#ifdef EFI_VENDOR_NAME
#define EFI_VENDOR_PATH "\\EFI\\" EFI_VENDOR_NAME "\\"
#else
#define EFI_VENDOR_PATH "\\EFI\\BOOT\\"
#endif
CONST CHAR16* KERNEL_PATH = L"" EFI_VENDOR_PATH "KERNEL.BIN";

CONST EFI_GUID EfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
CONST EFI_GUID EfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
CONST EFI_GUID EfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
CONST EFI_GUID efi_acpi_20_table_guid = EFI_ACPI_20_TABLE_GUID;
CONST EFI_GUID smbios3_table_guid = SMBIOS3_TABLE_GUID;
CONST EFI_GUID EfiShellParametersProtocolGuid = EFI_SHELL_PARAMETERS_PROTOCOL_GUID;

int is_64bit_processor(void);

#define	EFI_PRINT(s)	gST->ConOut->OutputString(st->ConOut, L ## s)

EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
EFI_RUNTIME_SERVICES *gRT;

/*********************************************************************/

void* memcpy(void* p, const void* q, size_t n) {
    uint8_t* _p = (uint8_t*)p;
    const uint8_t* _q = (const uint8_t*)q;
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < n; i++) {
        *_p++ = *_q++;
    }
    return p;
}

void* memset(void * p, int v, size_t n) {
    uint8_t* _p = (uint8_t*)p;
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < n; i++) {
        *_p++ = v;
    }
    return p;
}

static void* malloc(size_t n) {
    void* result = 0;
    EFI_STATUS status = gST->BootServices->AllocatePool(EfiLoaderData, n, &result);
    if (EFI_ERROR(status)){
        return 0;
    }
    return result;
}

static void free(void* p) {
    if (p) {
        gBS->FreePool(p);
    }
}

size_t wcslen(const wchar_t *s) {
    size_t count = 0;
    for (; s[count]; count++) {}
    return count;
}


static EFI_STATUS efi_get_file_content(IN EFI_FILE_HANDLE fs, IN CONST CHAR16* path, OUT struct iovec* result) {
    EFI_STATUS status;
    EFI_FILE_HANDLE handle = NULL;
    void* buff = NULL;
    uint64_t fsize = UINT64_MAX;

    //  Open file
    status = fs->Open(fs, &handle, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) return status;

    //  Get file size
    status = handle->SetPosition(handle, fsize);
    if (EFI_ERROR(status)) goto error;
    status = handle->GetPosition(handle, &fsize);
    if (EFI_ERROR(status)) goto error;
    status = handle->SetPosition(handle, 0);
    if (EFI_ERROR(status)) goto error;

    //  Allocate memory
    if ((sizeof(UINTN) < sizeof(uint64_t)) && fsize > UINT32_MAX) {
        status = EFI_OUT_OF_RESOURCES;
        goto error;
    }
    buff = malloc(fsize);
    if (!buff){
        status = EFI_OUT_OF_RESOURCES;
        goto error;
    }

    //  Read
    UINTN read_count = fsize;
    status = handle->Read(handle, &read_count, buff);
    if (EFI_ERROR(status)) goto error;
    status = handle->Close(handle);
    if (EFI_ERROR(status)) goto error;

    result->iov_base = buff;
    result->iov_len = read_count;

    return EFI_SUCCESS;

error:
    if (buff) {
        free(buff);
    }
    if (handle) {
        handle->Close(handle);
    }
    return status;
}


static inline int IsEqualGUID(CONST EFI_GUID* guid1, CONST EFI_GUID* guid2) {
    uint64_t* p = (uint64_t*)guid1;
    uint64_t* q = (uint64_t*)guid2;
    return (p[0] == q[0]) && (p[1] == q[1]);
}

static void* efi_find_config_table(CONST EFI_GUID* guid) {
    for (int i = 0; i < gST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE* tab = gST->ConfigurationTable + i;
        if (IsEqualGUID(&tab->VendorGuid, guid)) {
            return tab->VendorTable;
        }
    }
    return NULL;
}

/*********************************************************************/

moe_bootinfo_t bootinfo;

EFI_STATUS EFIAPI efi_main(IN EFI_HANDLE image, IN EFI_SYSTEM_TABLE *st) {
    EFI_STATUS status;
    struct iovec kernel_vec;
    IMAGE_LOCATOR locator = NULL;

    // Init UEFI Environments
    gST = st;
    gBS = st->BootServices;
    gRT = st->RuntimeServices;

    // check processor
#if defined(__i386__)
    if (!is_64bit_processor()) {
        EFI_PRINT("This operating system needs 64bit processor\r\n");
        return EFI_UNSUPPORTED;
    }
#endif

    // Command line
    {
        EFI_SHELL_PARAMETERS_PROTOCOL *params;
        status = gBS->OpenProtocol(image, &EfiShellParametersProtocolGuid, (void **)&params, image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
        if (!EFI_ERROR(status)) {
            size_t size = 0;
            for (size_t i = 1; i < params->Argc; i++) {
                size += 1 + wcslen(params->Argv[i]);
            }
            size = (size + 1) *  sizeof(wchar_t);
            wchar_t *buffer = malloc(size);
            size_t index = 0;
            for (size_t i = 1; i < params->Argc; i++) {
                buffer[index++] = ' ';
                size_t delta = wcslen(params->Argv[i]);
                memcpy(buffer + index, params->Argv[i], delta * sizeof(wchar_t));
                index += delta;
            }
            buffer[index] = 0;
            bootinfo.cmdline = (uintptr_t)buffer;
        } else {
            EFI_LOADED_IMAGE_PROTOCOL* li;
            status = gBS->OpenProtocol(image, &EfiLoadedImageProtocolGuid, (void **)&li, image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
            if (!EFI_ERROR(status)) {
                size_t option_size = li->LoadOptionsSize;
                if (option_size) {
                    size_t size = option_size + sizeof(wchar_t);
                    wchar_t *buffer = malloc(size);
                    memcpy(buffer, li->LoadOptions, option_size);
                    buffer[option_size / sizeof(wchar_t)] = 0;
                    bootinfo.cmdline = (uintptr_t)buffer;
                }
            }
        }
    }

    // Load kernel
    {
        EFI_FILE_PROTOCOL *file;
        EFI_LOADED_IMAGE_PROTOCOL* li;
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
        status = gBS->HandleProtocol(image, &EfiLoadedImageProtocolGuid, (void**)&li);
        if (EFI_ERROR(status)) return EFI_LOAD_ERROR;
        status = gBS->HandleProtocol(li->DeviceHandle, &EfiSimpleFileSystemProtocolGuid, (void**)&fs);
        if (EFI_ERROR(status)) return EFI_LOAD_ERROR;
        status = fs->OpenVolume(fs, &file);
        if (EFI_ERROR(status)) return EFI_LOAD_ERROR;

        status = efi_get_file_content(file, KERNEL_PATH, &kernel_vec);
        if (EFI_ERROR(status)) {
            EFI_PRINT("ERROR: KERNEL NOT FOUND\r\n");
            return EFI_NOT_FOUND;
        }
        locator = recognize_kernel_signature(kernel_vec);
        if (!locator) {
            EFI_PRINT("ERROR: BAD KERNEL SIGNATURE FOUND\r\n");
            return EFI_LOAD_ERROR;
        }
    }

    // Find ACPI table pointer
    {
        bootinfo.acpi = (uintptr_t)efi_find_config_table(&efi_acpi_20_table_guid);
        if (!bootinfo.acpi) {
            EFI_PRINT("ERROR: ACPI NOT FOUND\r\n");
            return EFI_LOAD_ERROR;
        }
    }

    // SMBIOS
    {
        bootinfo.smbios = (uintptr_t)efi_find_config_table(&smbios3_table_guid);
    }

    // Get GOP
    {
        EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;

        status = gBS->LocateProtocol(&EfiGraphicsOutputProtocolGuid, NULL, (void**)&gop);
        if (!gop) {
            EFI_PRINT("ERROR: GOP NOT FOUND\r\n");
            return EFI_LOAD_ERROR;
        }

        bootinfo.vram_base = gop->Mode->FrameBufferBase;
        bootinfo.screen.width = gop->Mode->Info->HorizontalResolution;
        bootinfo.screen.height = gop->Mode->Info->VerticalResolution;
        bootinfo.screen.delta = gop->Mode->Info->PixelsPerScanLine;
        if (bootinfo.screen.width > bootinfo.screen.delta) {
            // GPD Micro PC Pseudo Landscape Mode
            int temp = bootinfo.screen.width;
            bootinfo.screen.width = bootinfo.screen.height;
            bootinfo.screen.height = temp;
        }
    }

    // EFI TIME
    {
        EFI_TIME *time = (EFI_TIME *)(&bootinfo.boottime);
        gRT->GetTime(time, NULL);
    }

    // Exit BootServices
    {
        EFI_MEMORY_DESCRIPTOR *mmap = NULL;
        UINTN mmapsize = 0;
        UINTN mapkey, descriptorsize;
        UINT32 descriptorversion;

        do {
            status = gBS->GetMemoryMap(&mmapsize, mmap, &mapkey, &descriptorsize, &descriptorversion);
            while (status == EFI_BUFFER_TOO_SMALL) {
                if (mmap) {
                    gBS->FreePool(mmap);
                }
                status = gBS->AllocatePool(EfiLoaderData, mmapsize, (void **)&mmap);
                status = gBS->GetMemoryMap(&mmapsize, mmap, &mapkey, &descriptorsize, &descriptorversion);
            }
            status = gBS->ExitBootServices(image, mapkey);
        } while (EFI_ERROR(status));

        page_init(&bootinfo, mmap, mmapsize, descriptorsize);
        // bootinfo.efiRT = st->RuntimeServices;
    }

    uint64_t entry_point = locator(bootinfo.kernel_base);

    // Start Kernel
    size_t stack_size = 0x4000;
    uint64_t sp = bootinfo.kernel_base + 0x3FFFF000;
    valloc(sp - stack_size, stack_size);
    uint64_t params[2] = { entry_point, sp };
    start_kernel(&bootinfo, params);

}

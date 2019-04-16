// MEG-OS Loader for EFI
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "kernel.h"
#include "efi.h"

#define	INVALID_UNICHAR	0xFFFE
#define	ZWNBSP	0xFEFF
#ifdef EFI_VENDOR_NAME
#define EFI_VENDOR_PATH "\\EFI\\" EFI_VENDOR_NAME "\\"
#else
#define EFI_VENDOR_PATH "\\EFI\\BOOT\\"
#endif
CONST CHAR16* KERNEL_PATH = L"" EFI_VENDOR_PATH "KERNEL.EFI";

CONST EFI_GUID EfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
CONST EFI_GUID EfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
CONST EFI_GUID EfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
CONST EFI_GUID efi_acpi_20_table_guid = EFI_ACPI_20_TABLE_GUID;
CONST EFI_GUID smbios3_table_guid = SMBIOS3_TABLE_GUID;

extern int pe_preparse(void *obj, size_t size);
extern uint64_t pe_locate(uint64_t base);
extern void page_init(moe_bootinfo_t *bootinfo, void *mmap, size_t mmsize, size_t mmdescsize);
extern void *virtual_alloc(uint64_t base, size_t size, int attr);
extern int check_arch(void);
extern _Noreturn void start_kernel(moe_bootinfo_t* bootinfo, uint64_t* param);


#define	EFI_PRINT(s)	gST->ConOut->OutputString(st->ConOut, L ## s)

typedef struct {
	void* base;
	size_t size;
} base_and_size;

EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
EFI_RUNTIME_SERVICES *gRT;

/*********************************************************************/

int putchar(unsigned char c) {
    static uint8_t state[2] = {0};
    uint32_t ch = INVALID_UNICHAR;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *cout = gST->ConOut;

    if(c < 0x80) { // ASCII
        if(state[0] == 0){
            ch = c;
        } else {
            state[0] = 0;
        }
    } else if(c < 0xC0) { // Trail Bytes
        if(state[0] < 0xE0) { // 2bytes (Cx 8x .. Dx Bx)
            ch = ((state[0]&0x1F)<<6)|(c&0x3F);
            state[0] = 0;
        } else if (state[0] < 0xF0) { // 3bytes (Ex 8x 8x)
            if(state[1] == 0){
                state[1] = c;
                ch = ZWNBSP;
            } else {
                ch = ((state[0]&0x0F)<<12)|((state[1]&0x3F)<<6)|(c&0x3F);
                state[0] = 0;
            }
        }
    } else if(c >= 0xC2 && c <= 0xEF) { // Leading Byte
        state[0] = c;
        state[1] = 0;
        ch = ZWNBSP;
    } else { // invalid sequence
        state[0] = 0;
    }

    if(ch == INVALID_UNICHAR) ch ='?';

    switch(ch) {
        default:
        {
            CHAR16 box[] = { ch, 0 };
            EFI_STATUS status = cout->OutputString(cout, box);
            return status ? 0 : 1;
        }

        case '\n':
        {
            static CONST CHAR16* crlf = L"\r\n";
            EFI_STATUS status = cout->OutputString(cout, crlf);
            return status ? 0 : 1;
        }

        case ZWNBSP:
            return 0;
    }

}

// void* memcpy(void* p, const void* q, size_t n) {
//     uint8_t* _p = (uint8_t*)p;
//     const uint8_t* _q = (const uint8_t*)q;
//     #pragma clang loop vectorize(enable) interleave(enable)
//     for (int i = 0; i < n; i++) {
//         *_p++ = *_q++;
//     }
//     return p;
// }

// void* memset(void * p, int v, size_t n) {
//     uint8_t* _p = (uint8_t*)p;
//     #pragma clang loop vectorize(enable) interleave(enable)
//     for (int i = 0; i < n; i++) {
//         *_p++ = v;
//     }
//     return p;
// }

static void* malloc(size_t n) {
    void* result = 0;
    EFI_STATUS status = gST->BootServices->AllocatePool(EfiLoaderData, n, &result);
    if(EFI_ERROR(status)){
        return 0;
    }
    return result;
}

static void free(void* p) {
    if(p) {
        gBS->FreePool(p);
    }
}

static EFI_STATUS efi_get_file_content(IN EFI_FILE_HANDLE fs, IN CONST CHAR16* path, OUT base_and_size* result) {
    EFI_STATUS status;
    EFI_FILE_HANDLE handle = NULL;
    void* buff = NULL;
    uint64_t fsize = UINT64_MAX;

    //  Open file
    status = fs->Open(fs, &handle, path, EFI_FILE_MODE_READ, 0);
    if(EFI_ERROR(status)) return status;

    //  Get file size
    status = handle->SetPosition(handle, fsize);
    if(EFI_ERROR(status)) goto error;
    status = handle->GetPosition(handle, &fsize);
    if(EFI_ERROR(status)) goto error;
    status = handle->SetPosition(handle, 0);
    if(EFI_ERROR(status)) goto error;

    //  Allocate memory
    if ((sizeof(UINTN) < sizeof(uint64_t)) && fsize > UINT32_MAX) {
        status = EFI_OUT_OF_RESOURCES;
        goto error;
    }
    buff = malloc(fsize);
    if(!buff){
        status = EFI_OUT_OF_RESOURCES;
        goto error;
    }

    //  Read
    UINTN read_count = fsize;
    status = handle->Read(handle, &read_count, buff);
    if(EFI_ERROR(status)) goto error;
    status = handle->Close(handle);
    if(EFI_ERROR(status)) goto error;

    result->base = buff;
    result->size = read_count;

    return EFI_SUCCESS;

error:
    if(buff) {
        free(buff);
    }
    if(handle) {
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
    base_and_size kernel_ptr;

    // Init UEFI Environments
    gST = st;
    gBS = st->BootServices;
    gRT = st->RuntimeServices;

    // check processor
    {
        status = check_arch();
        if (status != 0) {
            EFI_PRINT("This operating system requires 64bit processor\r\n");
            return EFI_UNSUPPORTED;
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

        status = efi_get_file_content(file, KERNEL_PATH, &kernel_ptr);
        if (EFI_ERROR(status)) {
            EFI_PRINT("ERROR: KERNEL NOT FOUND\r\n");
            return EFI_NOT_FOUND;
        }
        status = pe_preparse(kernel_ptr.base, kernel_ptr.size);
        if (status < 0) {
            EFI_PRINT("ERROR: BAD KERNEL SIGNATURE FOUND\r\n");
            goto errexit;
        }
    }

    // Find ACPI table pointer
    {
        bootinfo.acpi = (uintptr_t)efi_find_config_table(&efi_acpi_20_table_guid);
        if (!bootinfo.acpi) {
            EFI_PRINT("ERROR: ACPI NOT FOUND\r\n");
            goto errexit;
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
            goto errexit;
        }

        bootinfo.vram_base = gop->Mode->FrameBufferBase;
        bootinfo.screen.width = gop->Mode->Info->HorizontalResolution;
        bootinfo.screen.height = gop->Mode->Info->VerticalResolution;
        bootinfo.screen.delta = gop->Mode->Info->PixelsPerScanLine;
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

    uint64_t entry_point = pe_locate(bootinfo.kernel_base);

    // Start Kernel
    size_t stack_size = 0x4000;
    uint64_t sp = bootinfo.kernel_base + 0x3FFFF000;
    virtual_alloc(sp - stack_size, stack_size, 0);
    uint64_t params[2] = { entry_point, sp };
    start_kernel(&bootinfo, params);

errexit:
    return EFI_LOAD_ERROR;

}

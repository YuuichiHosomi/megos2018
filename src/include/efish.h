
#pragma once

#include "efi.h"

#define EFI_SHELL_MAJOR_VERSION 2
#define EFI_SHELL_MINOR_VERSION 2

typedef void * SHELL_FILE_HANDLE;


#define EFI_SHELL_PROTOCOL_GUID \
    { 0x6302d008, 0x7f9b, 0x4f30, { 0x87, 0xac, 0x60, 0xc9, 0xfe, 0xf5, 0xda, 0x4e }}

typedef struct _EFI_SHELL_PROTOCOL {
    // TODO: everything
    int _fixme;
} EFI_SHELL_PROTOCOL;


#define EFI_SHELL_PARAMETERS_PROTOCOL_GUID \
    { 0x752f3136, 0x4e16, 0x4fdc, { 0xa2, 0x2a, 0xe5, 0xf4, 0x68, 0x12, 0xf4, 0xca }}

typedef struct _EFI_SHELL_PARAMETERS_PROTOCOL {
    CHAR16 **Argv;
    UINTN Argc;
    SHELL_FILE_HANDLE StdIn;
    SHELL_FILE_HANDLE StdOut;
    SHELL_FILE_HANDLE StdErr;
} EFI_SHELL_PARAMETERS_PROTOCOL;

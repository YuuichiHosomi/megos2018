// PE.h
#include <stdint.h>


#define IMAGE_DOS_SIGNATURE             0x4D5A
#define IMAGE_NT_SIGNATURE              0x00004550


typedef struct {
    uint16_t machine;
    uint16_t n_sections;
    uint32_t time_stamp;
    uint32_t ptr_to_coff_symtab;
    uint32_t n_coff_symbols;
    uint16_t size_of_optional;
    uint16_t coff_flags;
} pe_coff_header_t;

#define IMAGE_FILE_MACHINE_AMD64        0x8664
#define IMAGE_FILE_MACHINE_ARM          0x01C0
#define IMAGE_FILE_MACHINE_THUMB        0x01C2
#define IMAGE_FILE_MACHINE_ARMNT        0x01C4
#define IMAGE_FILE_MACHINE_ARM64        0xAA64
#define IMAGE_FILE_MACHINE_EBC          0x0EBC
#define IMAGE_FILE_MACHINE_I386         0x014C
#define IMAGE_FILE_MACHINE_IA64         0x0200
#define IMAGE_FILE_MACHINE_RISCV32      0x5032
#define IMAGE_FILE_MACHINE_RISCV64      0x5064
#define IMAGE_FILE_MACHINE_RISCV128     0x5128

#define IMAGE_FILE_RELOCS_STRIPPED      0x0001
#define IMAGE_FILE_EXECUTABLE_IMAGE     0x0002
#define IMAGE_FILE_LINE_NUMS_STRIPPED   0x0004
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED  0x0008
#define IMAGE_FILE_MINIMAL_OBJECT       0x0010
#define IMAGE_FILE_UPDATE_OBJECT        0x0020
#define IMAGE_FILE_16BIT_MACHINE        0x0040
#define IMAGE_FILE_BYTES_REVERSED_LO    0x0080
#define IMAGE_FILE_32BIT_MACHINE        0x0100
#define IMAGE_FILE_DEBUG_STRIPPED       0x0200
#define IMAGE_FILE_PATCH                0x0400
#define IMAGE_FILE_SYSTEM               0x1000
#define IMAGE_FILE_DLL                  0x2000
#define IMAGE_FILE_BYTES_REVERSED_HI    0x8000


typedef struct {
    uint16_t magic;
    uint8_t major_linker_version, minor_linker_version;
    uint32_t size_of_code, size_of_data, size_of_bss, entry_point, base_of_code;
    uint64_t image_base;
    uint32_t section_align, file_align;
    uint16_t major_os_version, minor_os_version;
    uint16_t major_image_version, minor_image_version;
    uint16_t major_subsys_version, minor_subsys_version;
    uint32_t win32_reserved;
    uint32_t size_of_image, size_of_headers;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_flags;
    uint64_t size_of_stack_reserve, size_of_stack_commit, size_of_heap_reserve, size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t numer_of_dir;
} __attribute__((packed)) pe_pe64_optional_header_t;

#define MAGIC_PE32                  0x010B
#define MAGIC_PE64                  0x020B

#define IMAGE_SUBSYSTEM_EFI_APPLICATION 10

typedef struct {
    uint32_t rva, size;
} pe_image_data_directory_t;

typedef struct {
    uint32_t pe_signature;
    pe_coff_header_t coff_header;
    pe_pe64_optional_header_t optional_header;
    pe_image_data_directory_t dir[16];
} pe64_header_t;

#define IMAGE_DIRECTORY_ENTRY_EXPORT        0
#define IMAGE_DIRECTORY_ENTRY_IMPORT        1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE      2
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION     3
#define IMAGE_DIRECTORY_ENTRY_SECURITY      4
#define IMAGE_DIRECTORY_ENTRY_BASERELOC     5
#define IMAGE_DIRECTORY_ENTRY_DEBUG         6
#define IMAGE_DIRECTORY_ENTRY_COPYRIGHT     7
#define IMAGE_DIRECTORY_ENTRY_GLOBALPTR     8
#define IMAGE_DIRECTORY_ENTRY_TLS           9
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG   10
#define IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT  11
#define IMAGE_DIRECTORY_ENTRY_IAT           12


typedef struct {
    char name[8];
    uint32_t vsize,rva;
    uint32_t size, file_offset;
    uint32_t ptr_reloc, ptr_lineno;
    uint16_t n_reloc, n_lineno;
    uint32_t flags;
} pe_section_table_t;

#define IMAGE_SCN_TYPE_REGULAR              0x00000000
#define IMAGE_SCN_TYPE_DUMMY                0x00000001
#define IMAGE_SCN_TYPE_NO_LOAD              0x00000002
#define IMAGE_SCN_TYPE_GROUPED              0x00000004
#define IMAGE_SCN_TYPE_NO_PAD               0x00000008
#define IMAGE_SCN_TYPE_COPY                 0x00000010
#define IMAGE_SCN_CNT_CODE                  0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA      0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA    0x00000080
#define IMAGE_SCN_LNK_OTHER                 0x00000100
#define IMAGE_SCN_LNK_INFO                  0x00000200
#define IMAGE_SCN_LNK_OVERLAY               0x00000400
#define IMAGE_SCN_LNK_REMOVE                0x00000800
#define IMAGE_SCN_LNK_COMDAT                0x00001000
#define IMAGE_SCN_MEM_DISCARDABLE           0x02000000
#define IMAGE_SCN_MEM_NOT_CACHED            0x04000000
#define IMAGE_SCN_MEM_NOT_PAGED             0x08000000
#define IMAGE_SCN_MEM_SHARED                0x10000000
#define IMAGE_SCN_MEM_EXECUTE               0x20000000
#define IMAGE_SCN_MEM_READ                  0x40000000
#define IMAGE_SCN_MEM_WRITE                 0x80000000

typedef struct {
    uint32_t rva_base;
    uint32_t size;
    struct {
        uint16_t value:12;
        uint16_t type:4;
    } entry[1];
} pe_basereloc_t;

#define IMAGE_REL_BASED_ABSOLUTE            0
#define IMAGE_REL_BASED_HIGH                1
#define IMAGE_REL_BASED_LOW                 2
#define IMAGE_REL_BASED_HIGHLOW             3
#define IMAGE_REL_BASED_HIGHADJ             4
#define IMAGE_REL_BASED_MIPS_JMPADDR        5
#define IMAGE_REL_BASED_ARM_MOV32           5
#define IMAGE_REL_BASED_RISCV_HIGH20        5
#define IMAGE_REL_BASED_THUMB_MOV32         7
#define IMAGE_REL_BASED_RISCV_LOW12I        7
#define IMAGE_REL_BASED_RISCV_LOW12S        8
#define IMAGE_REL_BASED_MIPS_JMPADDR16      9
#define IMAGE_REL_BASED_DIR64               10

// CPUID pseudo application
#include "moe.h"
#include "x86.h"


typedef union {
    struct {
        uint32_t eax, ebx, ecx, edx;
    };
    char bytes[16];
} cpuid_result_t;

typedef struct {
    int reg_index, bit_index;
    const char *label;
} cpuid_feature_table_t;

enum {
    CPUID_FEND,
    CPUID_F01D,
    CPUID_F01C,
    CPUID_F07B,
    CPUID_F81D,
    CPUID_F81C,
};

cpuid_feature_table_t isa_table[] = {
    {CPUID_F01D, 30, "IA64"},
    {CPUID_F81D, 29, "AMD64"},
    {CPUID_F01D, 0,  "FPU"},
    {CPUID_F01D, 23, "MMX"},
    {CPUID_F01D, 25, "SSE"},
    {CPUID_F01D, 26, "SSE2"},
    {CPUID_F01C, 0,  "SSE3"},
    {CPUID_F01C, 9,  "SSSE3"},
    {CPUID_F01C, 19, "SSE4.1"},
    {CPUID_F01C, 20, "SSE4.2"},
    {CPUID_F01C, 28, "AVX"},
    {CPUID_F07B, 5,  "AVX2"},
    {CPUID_F07B, 16, "AVX512F"},
    {CPUID_F01C, 25, "AES"},
    {CPUID_F01C, 12, "FMA3"},
    {CPUID_FEND}
};

cpuid_feature_table_t sysisa_table[] = {
    {CPUID_F01D, 6,  "PAE"},
    {CPUID_F81D, 20, "NX"},
    {CPUID_F01D, 28, "HT"},
    {CPUID_F01D, 24, "FXSR"},
    {CPUID_F01C, 26, "XSAVE"},
    {CPUID_F01C, 27, "OSXSAVE"},
    {CPUID_F07B, 0,  "FSGSBASE"},
    {CPUID_F01C, 5,  "VT-x"},
    {CPUID_F81C, 2,  "AMD-v"},
    {CPUID_F01C, 31, "HYPERVISOR"},
    {CPUID_FEND}
};

cpuid_result_t cpuid(uint32_t eax) {
    cpuid_result_t result;
    __asm__ volatile ("cpuid"
        :"=a"(result.eax),"=b"(result.ebx),"=c"(result.ecx),"=d"(result.edx)
        :"a"(eax)
        );
    return result;
}


int cmd_cpuid(int argc, char **argv) {
    cpuid_result_t cpuid_00, cpuid_01, cpuid_07, cpuid_80, cpuid_81, cpuid_8234[3];
    char cpuid_vendor[12];
    char *brand_string;

    cpuid_00 = cpuid(0x00000000);
    cpuid_01 = cpuid(0x00000001);
    if (cpuid_00.eax >= 0x07) {
        cpuid_07 = cpuid(0x00000007);
    } else {
        memset(&cpuid_07, 0, sizeof(cpuid_result_t));
    }
    cpuid_80 = cpuid(0x80000000);
    cpuid_81 = cpuid(0x80000001);
    if (cpuid_80.eax >= 0x80000004) {
        for (int i = 0; i < 3; i++) {
            cpuid_8234[i] = cpuid(0x80000002 + i);
        }
    } else {
        memset(cpuid_8234[0].bytes, 0, 48);
    }

    for (int i = 4; i < 16; i++) {
        int x = i ^ ((i > 7) ? 4 : 0);
        cpuid_vendor[i - 4] = cpuid_00.bytes[x];
    }

    if (cpuid_80.eax < 0x80000004) {
        brand_string = "";
    } else {
        char *p = cpuid_8234[0].bytes;
        while (*p == ' ') p++;
        brand_string = p;
    }

    printf("CPUID: %.12s %08x %s\n", cpuid_vendor, cpuid_01.eax, brand_string);

    uint32_t feature_regs[] = { 0, cpuid_01.edx, cpuid_01.ecx, cpuid_07.edx, cpuid_81.edx, cpuid_81.ecx, };

    printf("ISA:");
    for (int i = 0; isa_table[i].reg_index; i++) {
        cpuid_feature_table_t feature = isa_table[i];
        if (feature_regs[feature.reg_index] & (1 << feature.bit_index)) {
            printf(" %s", feature.label);
        }
    }
    printf("\n");

    printf("SYS:");
    for (int i = 0; sysisa_table[i].reg_index; i++) {
        cpuid_feature_table_t feature = sysisa_table[i];
        if (feature_regs[feature.reg_index] & (1 << feature.bit_index)) {
            printf(" %s", feature.label);
        }
    }
    printf("\n");


    return 0;
}

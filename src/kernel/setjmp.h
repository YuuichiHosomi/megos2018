#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long long jmp_buf[16];

#define setjmp _setjmp
#define longjmp _longjmp

intptr_t _setjmp(jmp_buf);
void _longjmp(jmp_buf, intptr_t) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

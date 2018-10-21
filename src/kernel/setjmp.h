#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long long jmp_buf[16];

#define setjmp _setjmp
#define longjmp _longjmp

int _setjmp(jmp_buf);
void _longjmp(jmp_buf, int) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

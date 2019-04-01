#pragma once

#include <stdint.h>
#include <stdnoreturn.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long long jmp_buf[16];

#define setjmp _setjmp
#define longjmp _longjmp

int _setjmp(jmp_buf);
noreturn void _longjmp(jmp_buf, int);

#ifdef __cplusplus
}
#endif

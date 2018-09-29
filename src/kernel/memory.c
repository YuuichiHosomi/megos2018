//  Generic Mermory Functions

#include <stdint.h>

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

void memset32(uint32_t* p, uint32_t v, size_t n) {
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < n; i++) {
        *p++ = v;
    }
}

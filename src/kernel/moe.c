// Minimal Operating Environment - Kernel
// Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
#include <stdatomic.h>
#include "moe.h"
#include "efi.h"


#define VER_SYSTEM_NAME     "Minimal Operating Environment"
#define VER_SYSTEM_MAJOR    0
#define VER_SYSTEM_MINOR    3
#define VER_SYSTEM_REVISION 2


void arch_init();
void acpi_init(acpi_rsd_ptr_t* rsd);
void mgs_init(moe_video_info_t* _video);
void mm_init(moe_bootinfo_mmap_t* mmap);
void hid_init();


/*********************************************************************/

void memset32(uint32_t* p, uint32_t v, size_t n) {
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < n; i++) {
        *p++ = v;
    }
}

/*********************************************************************/

int rgb32_to_luminance(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return ((r * 19589 + g * 38444 + b * 7502) + 32767) >> 16;
}

void draw_logo_bitmap(moe_video_info_t* video, const uint8_t* bmp, int offset_x, int offset_y) {
    uintptr_t offset = *((uint32_t*)(bmp + 10));
    int bmp_w = *((uint32_t*)(bmp + 18));
    int bmp_h = *((uint32_t*)(bmp + 22));
    int bmp_bpp = *((uint16_t*)(bmp + 28));
    int bmp_bpp8 = (bmp_bpp + 7) / 8;
    int bmp_ppl = (bmp_bpp8 * bmp_w + 3) & 0xFFFFFFFC;
    int delta = video->pixel_per_scan_line;
    const uint8_t* dib = bmp+offset;

    if (offset_x < 0) offset_x = (video->res_x - bmp_w) / 2;
    if (offset_y < 0) offset_y = (video->res_y - bmp_h) / 2;

    uint32_t* vram = (uint32_t*)video->vram;
    vram += offset_x + offset_y * delta;

    switch (bmp_bpp) {
        case 24:
        case 32:
            for (int i = bmp_h-1; i >= 0; i--) {
                const uint8_t* p = dib + (i * bmp_ppl);
                for (int j = 0; j < bmp_w; j++) {
                    uint32_t rgb = (p[j * bmp_bpp8 + 0]) + (p[j * bmp_bpp8 + 1] * 0x100) + (p[j * bmp_bpp8 + 2] * 0x10000);
                    if (rgb) {
                        uint8_t b = 255 - rgb32_to_luminance(rgb);
                        vram[j] = b + (b<<8) + (b<<16);
                    }
                }
                vram += delta;
            }
            break;

        case 8:
            for (int i = bmp_h-1; i >= 0; i--) {
                const uint8_t* p = dib + (i * bmp_ppl);
                for (int j = 0; j < bmp_w; j++) {
                    float alpha = p[j * bmp_bpp8 + 0] / 255.0;
                    uint8_t* vram8 = (uint8_t*)(vram+j);
                    for (int k = 0; k < 3; k++) {
                        vram8[k] = vram8[k] * alpha;
                    }
                }
                vram += delta;
            }
            break;
    }
}

/*********************************************************************/

#include "setjmp.h"

#define DEFAULT_QUANTUM 5;

extern void setjmp_new_thread(jmp_buf env, uintptr_t* new_sp);

typedef int thid_t;

typedef struct _moe_fiber_t moe_fiber_t;

#define THREAD_NAME_SIZE    32
typedef struct _moe_fiber_t {
    moe_fiber_t* next;
    thid_t      thid;
    uint8_t quantum_base;
    atomic_char quantum_left;
    atomic_flag lock;
    union {
        uintptr_t flags;
        struct {
            uintptr_t hoge:1;
        };
    };
    _Atomic uint64_t measure0;
    _Atomic uint64_t cputime;
    _Atomic uint32_t load0, load;
    void *fpu_context;
    jmp_buf jmpbuf;
    const char name[THREAD_NAME_SIZE];
} moe_fiber_t;

atomic_int next_thid = 1;
moe_fiber_t *current_thread;
moe_fiber_t *fpu_owner = 0;
moe_fiber_t root_thread;

int moe_get_current_thread() {
    return current_thread->thid;
}

void io_set_lazy_fpu_switch();
void io_finit();
void io_fsave(void*);
void io_fload(void*);

uint64_t moe_get_current_load() {
    return moe_get_measure() - current_thread->measure0;
}

//  Main Context Swicth
void moe_switch_context(moe_fiber_t* next) {
    MOE_ASSERT(current_thread != next, "CONTEXT SWITCH SKIPPING (%zx %zx)\n", current_thread, current_thread->thid);
    if (current_thread == next) return;

    if (atomic_flag_test_and_set(&current_thread->lock)) return;

    uint64_t load = moe_get_current_load();
    atomic_fetch_add(&current_thread->cputime, load);
    atomic_fetch_add(&current_thread->load0, load);
    if (!setjmp(current_thread->jmpbuf)) {
        if (fpu_owner != next) {
            io_set_lazy_fpu_switch();
        }
        current_thread = next;
        current_thread->measure0 = moe_get_measure();
        longjmp(next->jmpbuf, 0);
    }

    atomic_flag_clear(&current_thread->lock);
}

//  Lazy FPU Context Switch
void moe_switch_fpu_context(uintptr_t delta) {
    if (fpu_owner == current_thread) {
        ;
    } else {
        if (fpu_owner) {
            io_fsave(fpu_owner->fpu_context);
        }
        fpu_owner = current_thread;
        if (current_thread->fpu_context) {
            io_fload(current_thread->fpu_context);
        } else {
            io_finit();
            current_thread->fpu_context = mm_alloc_static(delta);
        }
    }
}

void moe_next_thread() {
    moe_fiber_t* next = current_thread->next;
    if (!next) next = &root_thread;
    moe_switch_context(next);
}

#define CONSUME_QUANTUM_THRESHOLD 100
void moe_consume_quantum() {
    // uint64_t load = moe_get_current_load();
    // current_thread->load = load;
    if (moe_get_current_load() > CONSUME_QUANTUM_THRESHOLD) {
        if (atomic_fetch_add(&current_thread->quantum_left, -1) <= 0) {
            atomic_fetch_add(&current_thread->quantum_left, current_thread->quantum_base);
            moe_next_thread();
        }
    }
}

void moe_yield() {
    if (current_thread->quantum_left > current_thread->quantum_base) {
        current_thread->quantum_left = current_thread->quantum_base;
    }
    moe_next_thread();
}

int moe_wait_for_timer(moe_timer_t* timer) {
    while (moe_check_timer(timer)) {
        moe_yield();
    }
    return 0;
}

int moe_usleep(uint64_t us) {
    moe_timer_t timer = moe_create_interval_timer(us);
    return moe_wait_for_timer(&timer);
}

int moe_create_thread(moe_start_thread start, void* args, const char* name) {

    moe_fiber_t* new_thread = mm_alloc_static(sizeof(moe_fiber_t));
    memset(new_thread, 0, sizeof(moe_fiber_t));
    // new_thread->lock = ATOMIC_FLAG_INIT;
    new_thread->thid = atomic_fetch_add(&next_thid, 1);
    new_thread->quantum_base = DEFAULT_QUANTUM;
    new_thread->quantum_left = 3 * DEFAULT_QUANTUM;
    if (name) {
        snprintf(&new_thread->name, THREAD_NAME_SIZE-1, "%s", name);
    }
    const uintptr_t stack_count = 0x1000;
    const uintptr_t stack_size = stack_count * sizeof(uintptr_t);
    uintptr_t* stack = mm_alloc_static(stack_size);
    memset(stack, 0, stack_size);
    uintptr_t* sp = stack + stack_count;
    *--sp = 0x00007fffdeadbeef;
    *--sp = (uintptr_t)args;
    *--sp = (uintptr_t)start;
    setjmp_new_thread(new_thread->jmpbuf, sp);

    moe_fiber_t* p = &root_thread;
    for (; p->next; p = p->next) {}
    p->next = new_thread;

    // moe_switch_context(new_thread);

    return new_thread->thid;
}

void thread_init() {
    root_thread.quantum_base = 1;
    snprintf(&root_thread.name, THREAD_NAME_SIZE, "%s", "(idle)");
    current_thread = &root_thread;
}


_Noreturn void scheduler() {
    const int64_t CLEANUP_LOAD_TIME = 1000000;
    int64_t last_cleanup_load_measure = 0;
    for (;;) {
        int64_t measure = moe_get_measure();
        if ((measure - last_cleanup_load_measure) >= CLEANUP_LOAD_TIME) {
            moe_fiber_t* thread = &root_thread;
            for (; thread; thread = thread->next) {
                int load = thread->load0;
                thread->load = load;
                atomic_fetch_add(&thread->load0, -load);
            }
            last_cleanup_load_measure = moe_get_measure();
        }
        moe_yield();
    }
}



int vprintf(const char *format, va_list args);
void moe_assert(const char* file, uintptr_t line, ...) {
    // mgs_bsod();
	va_list list;
	va_start(list, line);

    printf("ASSERT(File %s Line %zu):", file, line);
    const char* msg = va_arg(list, const char*);
    vprintf(msg, list);

	va_end(list);
    // __asm__ volatile("int3");
    // for (;;) io_hlt();
}


/*********************************************************************/

typedef struct _moe_fifo_t {
    volatile intptr_t* data;
    atomic_uintptr_t read, write, free, count;
    uintptr_t mask, flags;
} moe_fifo_t;


void moe_fifo_init(moe_fifo_t** result, uintptr_t capacity) {
    moe_fifo_t* self = mm_alloc_static(sizeof(moe_fifo_t));
    self->data = mm_alloc_static(capacity * sizeof(uintptr_t));
    self->read = self->write = self->count = self->flags = 0;
    self->free = self->mask = capacity - 1;
    *result = self;
}

intptr_t moe_fifo_read(moe_fifo_t* self, intptr_t default_val) {
    uintptr_t count = self->count;
    while (count > 0) {
        if (atomic_compare_exchange_weak(&self->count, &count, count - 1)) {
            uintptr_t read_ptr = atomic_fetch_add(&self->read, 1);
            intptr_t retval = self->data[read_ptr & self->mask];
            atomic_fetch_add(&self->free, 1);
            return retval;
        } else {
            io_pause();
            count = self->count;
        }
    }
    return default_val;
}

int moe_fifo_write(moe_fifo_t* self, intptr_t data) {
    uintptr_t free = self->free;
    while (free > 0) {
        if (atomic_compare_exchange_strong(&self->free, &free, free - 1)) {
            uintptr_t write_ptr = atomic_fetch_add(&self->write, 1);
            self->data[write_ptr & self->mask] = data;
            atomic_fetch_add(&self->count, 1);
            return 0;
        } else {
            io_pause();
            free = self->free;
        }
    }
    return -1;
}

/*********************************************************************/

EFI_RUNTIME_SERVICES* gRT;

void dump_madt(uint8_t* p, size_t n) {
    for (int i = 0; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

extern int putchar(char);

int getchar() {
    for(;;) {
        int c = hid_getchar();
        if (c >= 0) {
            return c;
        }
        moe_yield();
    }
}

int read_cmdline(char* buffer, size_t max_len) {
    int cont_flag = 1;
    int len = 0, limit = max_len - 1;

    while (cont_flag) {
        uint32_t c = getchar();
        switch (c) {
            case '\x08': // bs
            case '\x7F': // del
                if (len > 0) {
                    len--;
                    printf("\b \b");
                    if (buffer[len] < 0x20) { // ^X
                        printf("\b \b");
                    }
                }
                break;

            case '\x0D': // cr
                cont_flag = 0;
                break;
            
            default:
                if (len < limit) {
                    if (c < 0x80) {
                        buffer[len++] = c;
                        if (c < 0x20) { // ^X
                            printf("^%c", c | 0x40);
                        } else {
                            putchar(c);
                        }
                    } else { // non ascii
                        printf("?+%04x", c);
                    }
                }
                break;
        }
    }
    buffer[len] = '\0';
    printf("\n");
    return len;
}

extern uintptr_t total_memory;
extern moe_video_info_t* video;

void moe_ctrl_alt_del() {
    gRT->ResetSystem(EfiResetWarm, 0, 0, NULL);
}

void fpu_thread(void* args) {
    double count = 0.0;
    double pi = 3.14;
    int pid = moe_get_current_thread();
    for (;;) {
        count += pi * pid;
        int b = (int)count;
        mgs_fill_rect(pid * 10, 2, 8, 8, b);
        moe_yield();
    }
}


void display_threads() {
    moe_fiber_t* p = &root_thread;
    printf("ID context  sse_ctx  flags         usage name\n");
    for (; p; p = p->next) {
        printf("%2d %08zx %08zx %08zx %2d/%2d %4u %s\n",
            (int)p->thid, (uintptr_t)p, (uintptr_t)p->fpu_context, p->flags,
            p->quantum_left, p->quantum_base, p->load / 1000,
            p->name);
    }
}

void io_out8(uint16_t port, uint8_t val);
void acpi_enable(int enabled) {
    acpi_fadt_t* fadt = acpi_enum_table_entry(0);
    if (fadt->SMI_CMD) {
        if (enabled) {
            io_out8(fadt->SMI_CMD, fadt->ACPI_ENABLE);
        } else {
            io_out8(fadt->SMI_CMD, fadt->ACPI_DISABLE);
        }
    }
}

void start_init(void* args) {

    // mgs_fill_rect( 50,  50, 300, 300, 0xFF77CC);
    // mgs_fill_rect(150, 150, 300, 300, 0x77FFCC);
    // mgs_fill_rect(250, 100, 300, 300, 0x77CCFF);

    //  Show BGRT (Boot Graphics Resource Table) from ACPI
    acpi_bgrt_t* bgrt = acpi_find_table(ACPI_BGRT_SIGNATURE);
    if (bgrt) {
        // draw_logo_bitmap(video, (uint8_t*)bgrt->Image_Address, bgrt->Image_Offset_X, bgrt->Image_Offset_Y);
    }

    printf("%s v%d.%d.%d [Memory %dMB]\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION, (int)(total_memory >> 8));
    // printf("Hello, world!\n");

    hid_init();

    for (int i = 0; i < 5; i++){
        moe_create_thread(&fpu_thread, 0, "FPU DEMO");
    }

    // display_threads();

    //  Pseudo shell
    {
        const size_t cmdline_size = 80;
        char* cmdline = mm_alloc_static(cmdline_size);

        // EFI_TIME time;
        // gRT->GetTime(&time, NULL);
        // printf("Current Time: %d-%02d-%02d %02d:%02d:%02d %04d\n", time.Year, time.Month, time.Day, time.Hour, time.Minute, time.Second, time.TimeZone);

        // printf("Checking Timer...");
        moe_usleep(1000000);
        // printf("Ok\n");

        for (;;) {
            printf("# ");
            read_cmdline(cmdline, cmdline_size);

            switch (cmdline[0]) {
                case 0:
                break;

                case '!':
                {
                    volatile uintptr_t* p = (uintptr_t*)0x7777deadbeef;
                    *p = *p;
                }
                    break;

                case 'h':
                    printf("mini shell commands:\n");
                    printf("  v   display Version\n");
                    printf("  t   display Thread list\n");
                    printf("  c   Clear screen\n");
                    printf("  f   add FPU test thread (experimental)\n");
                    printf("  a   display Acpi table (experimental)\n");
                    printf("  m   display Madt table (experimental)\n");
                    break;

                case 'v':
                    printf("%s v%d.%d.%d [Memory %dMB]\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION, (int)(total_memory >> 8));
                    break;

                case 'c':
                    mgs_cls();
                    break;

                case 't':
                    display_threads();
                    break;

                case 'f':
                    moe_create_thread(&fpu_thread, 0, "FPU");
                    printf("FPU Thread started\n");
                    break;

                case 'r':
                    gRT->ResetSystem(EfiResetWarm, 0, 0, NULL);
                    break;

                case 'q':
                    gRT->ResetSystem(EfiResetShutdown, 0, 0, NULL);
                    break;

                case 'a':
                {
                    switch(cmdline[1]) {
                        case '0':
                            acpi_enable(0);
                            break;
                        case '1':
                            acpi_enable(1);
                            break;
                        default:
                            int n = acpi_get_number_of_table_entries();
                            printf("ACPI Tables: %d\n", n);
                            for (int i = 0; i < n; i++) {
                                acpi_header_t* table = acpi_enum_table_entry(i);
                                if (table) {
                                    printf("%p: %.4s %d\n", (void*)table, table->signature, table->length);
                                }
                            }
                            break;
                    }
                    break;
                }

                case 'm':
                {
                    acpi_madt_t* madt = acpi_find_table(ACPI_MADT_SIGNATURE);
                    if (madt) {
                        printf("Dump of MADT:\n");
                        size_t max_length = madt->Header.length - 44;
                        uint8_t* p = madt->Structure;
                        for (size_t loc = 0; loc < max_length; ) {
                            size_t len = p[loc+1];
                            dump_madt(p+loc, len);
                            loc += len;
                        }
                    }
                    break;
                }

                default:
                    printf("Bad command or file name\n");
                    break;

            }
        }
    }

}

void start_kernel(moe_bootinfo_t* bootinfo) {

    gRT = bootinfo->efiRT;
    mgs_init(&bootinfo->video);
    mm_init(&bootinfo->mmap);
    thread_init();
    acpi_init(bootinfo->acpi);
    arch_init();

    moe_create_thread(&scheduler, 0, "(scheduler)");
    moe_create_thread(&start_init, 0, "main");

    //  Do Idle
    for (;;) io_hlt();

}

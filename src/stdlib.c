/*

	Subset of Standard C Library

	Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.

*/

#include "efi.h"
#include <stdarg.h>

int putchar(char c);


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

/*********************************************************************/


static int sprintf_num(char** _buffer, uintptr_t val, unsigned base, size_t width, char padding, size_t* _count, size_t _limit) {
	char* buffer = *_buffer;
	size_t count = *_count;
	int sign = 0;

	if (base == 10){
		intptr_t ival = (intptr_t)val;
		if (ival < 0) {
			sign = 1;
			val = 0 - ival;
		}
	}

	if (sign) {
		*buffer++ = '-';
		count++;
	}

	uintptr_t work = val;
	size_t content_size = 0;
	unsigned mod;
	do {
		work /= base;
		content_size++;
	} while (work);

	if (width > content_size) {
		content_size = width;
	}

	size_t limit = _limit - count;
	if (limit < content_size) {
		content_size = limit;
	}
	if (width) {
		for (int i = 0; i < width; i++) {
			buffer[i] = padding;
		}
	}

	for (int i = content_size - 1; i >= 0; i--) {
		mod = val % base;
		val /= base;
		buffer[i] = (mod<10) ? ('0'+mod) : ('a'-10+mod);
		if (!val) break;
	}

	*_buffer = buffer + content_size;
	*_count = count + content_size;
	return content_size;
}

int vsnprintf(char* buffer, size_t limit, const char* format, va_list args) {
	size_t count = 0;
	const char* p = format;
	char* q = buffer;
	for(;*p && count<limit;) {
		char c = *p++;
		if(c=='%') {
			size_t width = 0, dot_width = 0;
			int z_flag = 0;
			int l_flag = 0;
			int dot_width_enable = 0;
			char padding = ' ';
			c = *p++;

			if (c == '0') {
				padding = '0';
				c = *p++;
			}
			while (c >= '0' && c <= '9') {
				width = width*10 + (c-'0');
				c = *p++;
			}
			if (c == '.') {
				dot_width_enable = 1;
				c = *p++;
				while (c >= '0' && c <= '9') {
					dot_width = dot_width*10 + (c-'0');
					c = *p++;
				}
			}

			size_t slimit = limit;
			if (dot_width_enable && limit - count > dot_width) {
				slimit = count + dot_width;
			}

			for(;c == 'z';c=*p++) { z_flag=1; }
			for(;c == 'l';c=*p++) { l_flag=1; }

			switch(c) {
				case 'c':
					*q++ = va_arg(args, int);
					count++;
					break;
				case 's':
					{
						const char* r = va_arg(args, char*);
						if (!r) r = "(null)";
						for (; *r && count < slimit; count++) {
							*q++ = *r++;
						}
					}
					break;
				case 'S':
					{
						const CHAR16* r = va_arg(args, const CHAR16*);
						if (!r) r = L"(null)";
						for (; *r && count < slimit; count++) {
							CHAR16 ch = *r++;
							if (ch < 0x80) {
								*q++ = ch;
							} else if (ch < 0x0800) {
								uint8_t utf[] = { 0xC0|(ch>>6), 0x80|(ch&0x3F) };
								if (count + 1 < slimit) {
									count++;
									*q++ = utf[0];
									*q++ = utf[1];
								} else {
									break;
								}
							} else {
								uint8_t utf[] = { 0xE0|(ch>>12), 0x80|((ch>>6)&0x3F), 0x80|(ch&0x3F) };
								if (count + 2 < slimit) {
									count += 2;
									*q++ = utf[0];
									*q++ = utf[1];
									*q++ = utf[2];
								} else {
									break;
								}
							}
						}
					}
					break;
				case 'd':
				case 'u':
				case 'x':
					// TODO: 'u' is currently not suppported
					uintptr_t val;
					unsigned base;
					if(l_flag){
						val = va_arg(args, int64_t);
					} else if(z_flag) {
						val = va_arg(args, intptr_t);
					} else {
						val = va_arg(args, int32_t);
					}
					if (c == 'x')
						base = 16;
					else
						base = 10;

					sprintf_num(&q, val, base, width, padding, &count, limit);
					break;
				case 'p':
					sprintf_num(&q, va_arg(args, uintptr_t), 16, 2*sizeof(void*), '0', &count, limit);
					break;
			}

		}else{
			*q++=c;
			count++;
		}
	}

	if(count<limit) *q++ = '\0';

	return count;
}


int snprintf(char* buffer, size_t n, const char* format, ...) {
	va_list list;
	va_start(list, format);
	int retval = vsnprintf(buffer, n, format, list);
	va_end(list);
	return retval;
}

#define PRINTF_BUFFER_SIZE 0x1000
static char printf_buffer[PRINTF_BUFFER_SIZE];

int printf(const char* format, ...) {
	va_list list;
	va_start(list, format);

	int count = vsnprintf(printf_buffer, PRINTF_BUFFER_SIZE, format, list);
	int retval = 0;
	char* p = printf_buffer;
	for(int i=0; i<count; i++) {
		retval += putchar(*p++);
	}

	va_end(list);
	return retval;
}

int puts(const char* s) {
    return printf("%s\n", s);
}

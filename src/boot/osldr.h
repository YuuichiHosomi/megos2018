/*

	Include file for MEG-OS Loader

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
#ifndef INCLUDED_OSLDR_H
#define INCLUDED_OSLDR_H

#include <stdint.h>
#include <stddef.h>
#include "efi.h"

#define	INVALID_UNICHAR	0xFFFE
#define	ZWNBSP	0xFEFF

#if defined(__x86_64__)
#define EFI_SUFFIX	"x64"
#elif defined(__i386__)
#define EFI_SUFFIX	"ia32"
#elif defined(__arm__)
#define EFI_SUFFIX	"arm"
#elif defined(__aarch64__)
#define EFI_SUFFIX	"aa64"
#endif

extern EFI_SYSTEM_TABLE* gST;
extern EFI_BOOT_SERVICES* gBS;
extern EFI_RUNTIME_SERVICES* gRT;
extern EFI_HANDLE* image;

extern EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* cout;
extern EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;

typedef struct {
	void* base;
	size_t size;
} base_and_size;

typedef struct {
	const char* label;
	uintptr_t item_id;
} menuitem;

typedef struct {
	menuitem* items;
	char* string_pool;
	int item_count, max_items, selected_index, pool_size, pool_used;
} menu_buffer;

EFI_STATUS cp932_tbl_init(base_and_size);
EFI_STATUS cp932_font_init(base_and_size);
EFIAPI EFI_STATUS ATOP_init(IN EFI_GRAPHICS_OUTPUT_PROTOCOL* gop, OUT EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL** result);

EFI_INPUT_KEY efi_wait_any_key(BOOLEAN, int);
menu_buffer* init_menu();
uintptr_t show_menu(menu_buffer* items, const char* title, const char* caption);
EFI_STATUS menu_add(menu_buffer* buffer, const char* label, uintptr_t menu_id);
EFI_STATUS menu_add_separator(menu_buffer* buffer);
EFI_STATUS menu_add_format(menu_buffer* buffer, uintptr_t menu_id, const char* format, ...);

size_t strwidth(const char* s);
void print_center(int rows, const char* message);
void draw_title_bar(const char* title);

#endif

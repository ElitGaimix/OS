#ifndef KERNEL_KSHELL_H
#define KERNEL_KSHELL_H

#include <e820.h>

void print_prefix(void);
void println(const char *text, unsigned char color);
void print(const char *text, unsigned char color);
void kshell_init(const e820_entry_t *memory_map, e820_u32_t entry_count);
void test(void);
void shutdown(void);

#endif
#ifndef KERNEL_MAIN_H
#define KERNEL_MAIN_H

#include <e820.h>

void kmain(const e820_entry_t *memory_map, e820_u32_t entry_count);

#endif
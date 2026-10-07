#ifndef KERNEL_MEMORY_PHYSICAL_H
#define KERNEL_MEMORY_PHYSICAL_H

#include <e820.h>

typedef unsigned long long physical_u64_t;

void physical_memory_init(
    const e820_entry_t *memory_map,
    e820_u32_t entry_count,
    physical_u64_t kernel_start,
    physical_u64_t kernel_end);
physical_u64_t physical_page_alloc(void);
int physical_page_free(physical_u64_t address);
unsigned int physical_page_free_count(void);

#endif

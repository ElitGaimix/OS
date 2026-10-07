#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stddef.h>

void kernel_heap_init(void);
void *kmalloc(size_t size);
void kfree(void *pointer);

#endif

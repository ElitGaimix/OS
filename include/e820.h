#ifndef E820_H
#define E820_H

typedef unsigned int e820_u32_t;
typedef unsigned long long e820_u64_t;

typedef struct {
    e820_u32_t base_low;
    e820_u32_t base_high;
    e820_u32_t length_low;
    e820_u32_t length_high;
    e820_u32_t type;
    e820_u32_t attributes;
} e820_entry_t;

#endif
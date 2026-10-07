#include <kernel/memory/physical.h>

#define PAGE_SIZE 4096ULL
#define MAX_PHYSICAL_ADDRESS 0x100000000ULL
#define PAGE_COUNT (MAX_PHYSICAL_ADDRESS / PAGE_SIZE)
#define BITMAP_SIZE (PAGE_COUNT / 8)

static unsigned char used_pages[BITMAP_SIZE];
static unsigned char allocated_pages[BITMAP_SIZE];

static int bitmap_get(const unsigned char *bitmap, unsigned int page)
{
    return (bitmap[page / 8] >> (page % 8)) & 1;
}

static void bitmap_set(unsigned char *bitmap, unsigned int page)
{
    bitmap[page / 8] |= (unsigned char)(1U << (page % 8));
}

static void bitmap_clear(unsigned char *bitmap, unsigned int page)
{
    bitmap[page / 8] &= (unsigned char)~(1U << (page % 8));
}

static void update_range(physical_u64_t start, physical_u64_t end, int used)
{
    if (start >= MAX_PHYSICAL_ADDRESS)
        return;
    if (end > MAX_PHYSICAL_ADDRESS)
        end = MAX_PHYSICAL_ADDRESS;
    if (end <= start)
        return;

    unsigned int first_page = (unsigned int)(
        used ? start / PAGE_SIZE : (start + PAGE_SIZE - 1) / PAGE_SIZE);
    unsigned int end_page = (unsigned int)(
        used ? (end + PAGE_SIZE - 1) / PAGE_SIZE : end / PAGE_SIZE);

    for (unsigned int page = first_page; page < end_page; page++)
    {
        if (used)
            bitmap_set(used_pages, page);
        else
            bitmap_clear(used_pages, page);
        bitmap_clear(allocated_pages, page);
    }
}

static physical_u64_t entry_base(const e820_entry_t *entry)
{
    return ((physical_u64_t)entry->base_high << 32) | entry->base_low;
}

static physical_u64_t entry_length(const e820_entry_t *entry)
{
    return ((physical_u64_t)entry->length_high << 32) | entry->length_low;
}

void physical_memory_init(
    const e820_entry_t *memory_map,
    e820_u32_t entry_count,
    physical_u64_t kernel_start,
    physical_u64_t kernel_end)
{
    for (unsigned int i = 0; i < BITMAP_SIZE; i++)
    {
        used_pages[i] = 0xFF;
        allocated_pages[i] = 0;
    }

    if (memory_map)
    {
        for (e820_u32_t i = 0; i < entry_count; i++)
        {
            if (memory_map[i].type != 1)
                continue;

            physical_u64_t base = entry_base(&memory_map[i]);
            physical_u64_t length = entry_length(&memory_map[i]);
            physical_u64_t end = base + length;
            if (end < base)
                end = ~(physical_u64_t)0;
            update_range(base, end, 0);
        }

        for (e820_u32_t i = 0; i < entry_count; i++)
        {
            if (memory_map[i].type == 1)
                continue;

            physical_u64_t base = entry_base(&memory_map[i]);
            physical_u64_t length = entry_length(&memory_map[i]);
            physical_u64_t end = base + length;
            if (end < base)
                end = ~(physical_u64_t)0;
            update_range(base, end, 1);
        }
    }

    update_range(0, 0x100000ULL, 1);
    update_range(0x70000ULL, 0x76000ULL, 1);
    update_range(0x400000ULL, 0x600000ULL, 1);
    update_range(kernel_start, kernel_end, 1);
}

physical_u64_t physical_page_alloc(void)
{
    for (unsigned int page = 0; page < PAGE_COUNT; page++)
    {
        if (bitmap_get(used_pages, page))
            continue;

        bitmap_set(used_pages, page);
        bitmap_set(allocated_pages, page);
        return (physical_u64_t)page * PAGE_SIZE;
    }
    return 0;
}

int physical_page_free(physical_u64_t address)
{
    if ((address & (PAGE_SIZE - 1)) || address >= MAX_PHYSICAL_ADDRESS)
        return -1;

    unsigned int page = (unsigned int)(address / PAGE_SIZE);
    if (!bitmap_get(allocated_pages, page))
        return -1;

    bitmap_clear(allocated_pages, page);
    bitmap_clear(used_pages, page);
    return 0;
}

unsigned int physical_page_free_count(void)
{
    unsigned int count = 0;
    for (unsigned int page = 0; page < PAGE_COUNT; page++)
        if (!bitmap_get(used_pages, page))
            count++;
    return count;
}

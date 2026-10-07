#include <kernel/memory/heap.h>

#define HEAP_CAPACITY (256U * 1024U)
#define HEAP_ALIGNMENT 16U

typedef struct heap_block
{
    size_t size;
    struct heap_block *previous;
    struct heap_block *next;
    int free;
} heap_block_t;

static unsigned char heap_storage[HEAP_CAPACITY] __attribute__((aligned(16)));
static heap_block_t *heap_first;

static size_t align_size(size_t size)
{
    return (size + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
}

void kernel_heap_init(void)
{
    heap_first = (heap_block_t *)heap_storage;
    heap_first->size = HEAP_CAPACITY - sizeof(*heap_first);
    heap_first->previous = 0;
    heap_first->next = 0;
    heap_first->free = 1;
}

void *kmalloc(size_t size)
{
    if (!heap_first || size == 0 || size > HEAP_CAPACITY - sizeof(heap_block_t))
        return 0;

    size = align_size(size);
    for (heap_block_t *block = heap_first; block; block = block->next)
    {
        if (!block->free || block->size < size)
            continue;

        size_t remaining = block->size - size;
        if (remaining > sizeof(heap_block_t) + HEAP_ALIGNMENT)
        {
            heap_block_t *next =
                (heap_block_t *)((unsigned char *)(block + 1) + size);
            next->size = remaining - sizeof(*next);
            next->previous = block;
            next->next = block->next;
            next->free = 1;
            if (next->next)
                next->next->previous = next;
            block->next = next;
            block->size = size;
        }

        block->free = 0;
        return block + 1;
    }

    return 0;
}

void kfree(void *pointer)
{
    if (!pointer)
        return;

    unsigned char *address = pointer;
    if (address < heap_storage + sizeof(heap_block_t)
        || address >= heap_storage + sizeof(heap_storage))
        return;

    heap_block_t *block = (heap_block_t *)pointer - 1;
    if ((unsigned char *)block < heap_storage
        || (unsigned char *)block >= heap_storage + sizeof(heap_storage)
        || block->free)
        return;

    block->free = 1;
    if (block->next && block->next->free)
    {
        heap_block_t *next = block->next;
        block->size += sizeof(*next) + next->size;
        block->next = next->next;
        if (block->next)
            block->next->previous = block;
    }

    if (block->previous && block->previous->free)
    {
        heap_block_t *previous = block->previous;
        previous->size += sizeof(*block) + block->size;
        previous->next = block->next;
        if (previous->next)
            previous->next->previous = previous;
    }
}

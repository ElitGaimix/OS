#define MAX_TASKS PROCESS_MAX_TASKS
#define USER_BASE 0x400000ULL
#define USER_SIZE 0x200000ULL
#define PAGE_SIZE 4096ULL
#define PTE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
#define PTE_NO_EXECUTE (1ULL << 63)
#define USER_STACK_PAGES 4
#define USER_IMAGE_PAGES 256
#define USER_HEAP_START 0x500000ULL
#define USER_HEAP_PAGES 240
#define USER_HEAP_ALLOCATIONS 64
#define KERNEL_STACK_SIZE 16384
#define MAX_TASK_PAGES \
    (USER_IMAGE_PAGES + USER_STACK_PAGES + USER_HEAP_PAGES)
#define USER_VIRTUAL_PAGES (USER_SIZE / PAGE_SIZE)
#define USER_STACK (USER_BASE + USER_SIZE - 8)

#include <kernel/process/process.h>
#include <kernel/interrupts.h>
#include <kernel/memory/physical.h>
#include <kernel/serial.h>
#include <kernel/kshell.h>
#include <kernel/process/pit.h>
#include <kernel/fs/tinyfs.h>
#include <kernel/memory/heap.h>

extern u8 __kernel_start[];
extern u8 __kernel_end[];

struct program
{
    const char *name;
    const char *file;
    u64 argument;
};

static const struct program programs[] = {
    {"crash", "program.elf", 1},
    {"hello", "program.elf", 0},
    {"ticker", "ticker.elf", 0},
    {"badptr", "program.elf", 2},
    {"write", "program.elf", 3},
    {"kaccess", "program.elf", 4},
    {"heaptest", "program.elf", 5},
    {"quick", "program.elf", 7},
    {"waittest", "program.elf", 6},
    {"nx", "program.elf", 8},
};

enum task_state
{
    TASK_FREE,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE
};

struct cpu_context
{
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rdi;
    u64 rsi;
    u64 rbp;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

struct heap_allocation
{
    unsigned int first_page;
    unsigned int page_count;
    int used;
};

typedef struct __attribute__((packed))
{
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 program_header_offset;
    u64 section_header_offset;
    u32 flags;
    u16 header_size;
    u16 program_header_size;
    u16 program_header_count;
    u16 section_header_size;
    u16 section_header_count;
    u16 section_name_index;
} elf64_header_t;

typedef struct __attribute__((packed))
{
    u32 type;
    u32 flags;
    u64 offset;
    u64 virtual_address;
    u64 physical_address;
    u64 file_size;
    u64 memory_size;
    u64 alignment;
} elf64_program_header_t;

struct task
{
    int pid;
    int used;
    int user;
    enum task_state state;
    int exit_code;
    int parent_pid;
    int wait_pid;
    u64 wake_tick;
    u64 cr3;
    u64 pages[MAX_TASK_PAGES];
    unsigned int page_count;
    struct heap_allocation heap_allocations[USER_HEAP_ALLOCATIONS];
    u64 *context;
    u8 kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
};

static int next_pid = 1;
#define IDENTITY_PAGE_DIRECTORY_COUNT 4
static u64 page_tables[MAX_TASKS - 1]
    [2 + IDENTITY_PAGE_DIRECTORY_COUNT][512] __attribute__((aligned(4096)));
static u64 user_page_tables[MAX_TASKS - 1][512] __attribute__((aligned(4096)));
static struct task tasks[MAX_TASKS];
static struct task *current_task;
static u64 kernel_cr3;
static unsigned int current_index;
static int filesystem_ready;
static int nx_enabled;

extern void process_syscall_entry(void);
extern void process_restore_context(u64 *context) __attribute__((noreturn));
static int user_range_accessible(u64 address, u64 length);

// Merci github copilot qui regal j'ai horreur du context a save de merde a la con fdp
__asm__(
    ".text\n"
    ".global process_syscall_entry\n"
    ".type process_syscall_entry,@function\n"
    "process_syscall_entry:\n"
    "    cld\n"
    "    pushq %rax\n"
    "    pushq %rbx\n"
    "    pushq %rcx\n"
    "    pushq %rdx\n"
    "    pushq %rbp\n"
    "    pushq %rsi\n"
    "    pushq %rdi\n"
    "    pushq %r8\n"
    "    pushq %r9\n"
    "    pushq %r10\n"
    "    pushq %r11\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq %rsp, %rdi\n"
    "    andq $-16, %rsp\n"
    "    call process_syscall_dispatch\n"
    "    movq %rax, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %r11\n"
    "    popq %r10\n"
    "    popq %r9\n"
    "    popq %r8\n"
    "    popq %rdi\n"
    "    popq %rsi\n"
    "    popq %rbp\n"
    "    popq %rdx\n"
    "    popq %rcx\n"
    "    popq %rbx\n"
    "    popq %rax\n"
    "    iretq\n"
    ".size process_syscall_entry, .-process_syscall_entry\n"

    ".global process_restore_context\n"
    ".type process_restore_context,@function\n"
    "process_restore_context:\n"
    "    movq %rdi, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %r11\n"
    "    popq %r10\n"
    "    popq %r9\n"
    "    popq %r8\n"
    "    popq %rdi\n"
    "    popq %rsi\n"
    "    popq %rbp\n"
    "    popq %rdx\n"
    "    popq %rcx\n"
    "    popq %rbx\n"
    "    popq %rax\n"
    "    iretq\n"
    ".size process_restore_context, .-process_restore_context\n");

static const struct program *find_program(const char *name)
{
    for (unsigned int i = 0; i < sizeof(programs) / sizeof(programs[0]); i++)
        if (strcmp(programs[i].name, name) == 0)
            return &programs[i];
    return 0;
}

static u64 task_kernel_stack_top(const struct task *task)
{
    return (u64)(task->kernel_stack + sizeof(task->kernel_stack));
}

static void activate_task(struct task *task)
{
    current_task = task;
    if (task->user)
        gdt_set_kernel_stack(task_kernel_stack_top(task));
    __asm__ volatile("mov %0, %%cr3" : : "r"(task->cr3) : "memory");
}

static struct task *next_ready_task(unsigned int after)
{
    for (unsigned int offset = 1; offset <= MAX_TASKS; offset++)
    {
        unsigned int index = (after + offset) % MAX_TASKS;
        if (tasks[index].used && tasks[index].state == TASK_READY)
        {
            current_index = index;
            return &tasks[index];
        }
    }
    current_index = 0;
    return &tasks[0];
}

static void release_task_pages(struct task *task)
{
    unsigned int released_pages = 0;
    for (unsigned int i = 0; i < MAX_TASK_PAGES; i++)
    {
        if (!task->pages[i])
            continue;
        released_pages++;
        if (physical_page_free(task->pages[i]) != 0)
        {
            println("Memory allocator: could not release process page", 0x0C);
            serial_write("memory: page release failed\n");
        }
        task->pages[i] = 0;
    }
    task->page_count = 0;
    if (released_pages)
    {
        serial_write("process: pages released=");
        serial_write_u64(released_pages);
        serial_write("\n");
    }
}

static int track_task_page(struct task *task, u64 page)
{
    for (unsigned int i = 0; i < MAX_TASK_PAGES; i++)
    {
        if (task->pages[i])
            continue;
        task->pages[i] = page;
        task->page_count++;
        return 0;
    }
    return -1;
}

static u64 *task_user_page_table(unsigned int slot)
{
    return user_page_tables[slot - 1];
}

static u64 allocate_user_page(struct task *task, u64 *user_pt, u64 virtual_page)
{
    if (virtual_page >= USER_IMAGE_PAGES)
        return 0;

    if (user_pt[virtual_page] & 1)
        return user_pt[virtual_page] & PTE_ADDRESS_MASK;

    u64 physical_page = physical_page_alloc();
    if (!physical_page)
        return 0;
    if (track_task_page(task, physical_page) != 0)
    {
        physical_page_free(physical_page);
        return 0;
    }

    u8 *memory = (u8 *)physical_page;
    for (unsigned int i = 0; i < PAGE_SIZE; i++)
        memory[i] = 0;
    user_pt[virtual_page] =
        physical_page | 0x05 | (nx_enabled ? PTE_NO_EXECUTE : 0);
    return physical_page;
}

static int copy_to_user_pages(
    u64 *user_pt,
    u64 virtual_address,
    const u8 *source,
    u64 length)
{
    while (length)
    {
        u64 page_index = (virtual_address - USER_BASE) / PAGE_SIZE;
        u64 offset = (virtual_address - USER_BASE) % PAGE_SIZE;
        if (page_index >= USER_IMAGE_PAGES || !(user_pt[page_index] & 1))
            return -1;

        u64 amount = PAGE_SIZE - offset;
        if (amount > length)
            amount = length;
        u64 physical_page = user_pt[page_index] & PTE_ADDRESS_MASK;
        u8 *destination = (u8 *)(physical_page + offset);
        for (u64 i = 0; i < amount; i++)
            destination[i] = source[i];

        virtual_address += amount;
        source += amount;
        length -= amount;
    }
    return 0;
}

static int load_elf(struct task *task, unsigned int slot,
                    const struct program *program, u64 *entry_out)
{
    tinyfs_file_t file;
    if (tinyfs_open(program->file, &file) != 0
        || file.size < sizeof(elf64_header_t)
        || file.size > 128U * 1024U)
        return -1;

    u8 *image = kmalloc(file.size);
    if (!image)
        return -1;
    if (tinyfs_read(&file, 0, file.size, image) != 0)
    {
        kfree(image);
        return -1;
    }

    elf64_header_t *header = (elf64_header_t *)image;
    if (header->ident[0] != 0x7F || header->ident[1] != 'E'
        || header->ident[2] != 'L' || header->ident[3] != 'F'
        || header->ident[4] != 2 || header->ident[5] != 1
        || header->type != 2 || header->machine != 62
        || header->version != 1 || header->header_size != sizeof(*header)
        || header->program_header_size != sizeof(elf64_program_header_t)
        || header->program_header_count == 0
        || header->program_header_count > 32
        || header->program_header_offset > file.size
        || (u64)header->program_header_count
            > (file.size - header->program_header_offset)
                / sizeof(elf64_program_header_t))
    {
        kfree(image);
        return -1;
    }

    u64 *user_pt = task_user_page_table(slot);
    elf64_program_header_t *headers =
        (elf64_program_header_t *)(image + header->program_header_offset);
    int entry_is_executable = 0;

    for (u16 i = 0; i < header->program_header_count; i++)
    {
        elf64_program_header_t *segment = &headers[i];
        if (segment->type != 1)
            continue;
        if (segment->file_size > segment->memory_size
            || segment->offset > file.size
            || segment->file_size > file.size - segment->offset
            || segment->virtual_address < USER_BASE
            || segment->virtual_address >= USER_HEAP_START
            || segment->memory_size > USER_HEAP_START - segment->virtual_address)
        {
            kfree(image);
            return -1;
        }
        if (!segment->memory_size)
            continue;

        u64 segment_end = segment->virtual_address + segment->memory_size;
        if (segment_end < segment->virtual_address
            || segment_end > USER_HEAP_START)
        {
            kfree(image);
            return -1;
        }

        u64 first_page = (segment->virtual_address - USER_BASE) / PAGE_SIZE;
        u64 last_page =
            (segment_end - 1 - USER_BASE) / PAGE_SIZE;
        for (u64 page = first_page; page <= last_page; page++)
        {
            if (!allocate_user_page(task, user_pt, page))
            {
                kfree(image);
                return -1;
            }
            if (segment->flags & 1)
                user_pt[page] &= ~PTE_NO_EXECUTE;
            if (segment->flags & 2)
                user_pt[page] |= 0x02;
        }

        if (segment->file_size
            && copy_to_user_pages(
                user_pt,
                segment->virtual_address,
                image + segment->offset,
                segment->file_size) != 0)
        {
            kfree(image);
            return -1;
        }

        if ((segment->flags & 1)
            && header->entry >= segment->virtual_address
            && header->entry < segment_end)
            entry_is_executable = 1;
    }

    u64 entry = header->entry;
    kfree(image);
    if (!entry_is_executable)
        return -1;

    *entry_out = entry;
    return 0;
}

static u64 *schedule(u64 *context, int task_finished)
{
    unsigned int previous_index = current_index;
    u64 ticks = pit_get_ticks();

    for (unsigned int i = 1; i < MAX_TASKS; i++)
    {
        if (tasks[i].used && tasks[i].state == TASK_BLOCKED
            && tasks[i].wait_pid == 0 && tasks[i].wake_tick <= ticks)
            tasks[i].state = TASK_READY;
    }

    if (current_task)
    {
        if (task_finished)
        {
            serial_write("process: exit pid=");
            serial_write_u64((u64)current_task->pid);
            serial_write(" code=");
            serial_write_u64((u64)current_task->exit_code);
            serial_write("\n");
            release_task_pages(current_task);
            serial_write("memory: free_pages=");
            serial_write_u64(physical_page_free_count());
            serial_write("\n");
            struct task *parent = 0;
            for (unsigned int i = 0; i < MAX_TASKS; i++)
            {
                if (tasks[i].used && tasks[i].user
                    && tasks[i].pid == current_task->parent_pid)
                {
                    parent = &tasks[i];
                    break;
                }
            }
            if (parent && parent->state == TASK_BLOCKED
                && parent->wait_pid == current_task->pid)
            {
                struct cpu_context *parent_context =
                    (struct cpu_context *)parent->context;
                parent_context->rax = (u64)current_task->exit_code;
                parent->wait_pid = 0;
                parent->state = TASK_READY;
                current_task->used = 0;
                current_task->state = TASK_FREE;
            }
            else
            {
                current_task->state = TASK_ZOMBIE;
                current_task->context = 0;
            }

            for (unsigned int i = 1; i < MAX_TASKS; i++)
            {
                if (!tasks[i].used
                    || tasks[i].parent_pid != current_task->pid)
                    continue;
                tasks[i].parent_pid = 0;
                if (tasks[i].state == TASK_ZOMBIE)
                {
                    tasks[i].used = 0;
                    tasks[i].state = TASK_FREE;
                }
            }
        }
        else
        {
            current_task->context = context;
            if (current_task->state == TASK_RUNNING)
                current_task->state = TASK_READY;
        }
    }

    struct task *next = next_ready_task(previous_index);
    next->state = TASK_RUNNING;
    activate_task(next);
    return next->context;
}

void process_init(const e820_entry_t *memory_map, e820_u32_t entry_count)
{
    u32 efer_low, efer_high;
    __asm__ volatile(
        "rdmsr"
        : "=a"(efer_low), "=d"(efer_high)
        : "c"(0xC0000080));
    (void)efer_high;
    nx_enabled = (efer_low & (1U << 11)) != 0;

    physical_memory_init(
        memory_map,
        entry_count,
        (u64)__kernel_start,
        (u64)__kernel_end);
    serial_write("memory: free_pages=");
    serial_write_u64(physical_page_free_count());
    serial_write("\n");
    filesystem_ready = tinyfs_init() == 0;

    __asm__ volatile("mov %%cr3, %0" : "=r"(kernel_cr3));

    for (unsigned int i = 0; i < MAX_TASKS; i++)
    {
        tasks[i].used = 0;
        tasks[i].user = 0;
        tasks[i].state = TASK_FREE;
        tasks[i].exit_code = 0;
        tasks[i].parent_pid = 0;
        tasks[i].wait_pid = 0;
        tasks[i].wake_tick = 0;
        tasks[i].cr3 = 0;
        tasks[i].page_count = 0;
        for (unsigned int page = 0; page < MAX_TASK_PAGES; page++)
            tasks[i].pages[page] = 0;
        for (unsigned int allocation = 0;
             allocation < USER_HEAP_ALLOCATIONS;
             allocation++)
            tasks[i].heap_allocations[allocation].used = 0;
        tasks[i].context = 0;
    }

    struct task *shell = &tasks[0];
    shell->used = 1;
    shell->user = 0;
    shell->state = TASK_RUNNING;
    shell->cr3 = kernel_cr3;
    shell->pid = 0;
    current_task = shell;
    current_index = 0;

    interrupt_register_user_handler(
        0x80,
        (u64)(unsigned long long)process_syscall_entry);
}

static int process_create(const char *name, int parent_pid)
{
    const struct program *program = find_program(name);
    if (!program)
        return -1;
    if (!filesystem_ready)
        return -3;

    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++)
    {
        if (!tasks[i].used)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -2;

    struct task *task = &tasks[slot];
    task->used = 1;
    task->user = 1;
    task->state = TASK_FREE;
    task->exit_code = 0;
    task->parent_pid = parent_pid;
    task->wait_pid = 0;
    task->wake_tick = 0;
    task->page_count = 0;
    for (unsigned int allocation = 0;
         allocation < USER_HEAP_ALLOCATIONS;
         allocation++)
        task->heap_allocations[allocation].used = 0;
    task->pid = next_pid;
    next_pid++;

    u64 *pml4 = page_tables[slot - 1][0];
    u64 *pdpt = page_tables[slot - 1][1];
    u64 *user_pt = user_page_tables[slot - 1];
    for (int i = 0; i < 512; i++)
    {
        pml4[i] = 0;
        pdpt[i] = 0;
        user_pt[i] = 0;
    }
    pml4[0] = (u64)pdpt | 0x07;
    for (unsigned int directory = 0;
         directory < IDENTITY_PAGE_DIRECTORY_COUNT;
         directory++)
    {
        u64 *pd = page_tables[slot - 1][2 + directory];
        pdpt[directory] = (u64)pd | 0x07;
        for (unsigned int page = 0; page < 512; page++)
        {
            u64 physical = ((u64)directory * 512 + page) << 21;
            pd[page] = physical | 0x83;
        }
    }
    page_tables[slot - 1][2][USER_BASE >> 21] =
        (u64)user_pt | 0x07;

    u64 entry_point = 0;
    if (load_elf(task, (unsigned int)slot, program, &entry_point) != 0)
    {
        release_task_pages(task);
        task->used = 0;
        return -4;
    }

    for (unsigned int i = 0; i < USER_STACK_PAGES; i++)
    {
        unsigned int page_index = USER_VIRTUAL_PAGES - USER_STACK_PAGES + i;
        u64 page = physical_page_alloc();
        if (!page || track_task_page(task, page) != 0)
        {
            if (page)
                physical_page_free(page);
            release_task_pages(task);
            task->used = 0;
            return -4;
        }
        u8 *memory = (u8 *)page;
        for (unsigned int byte = 0; byte < PAGE_SIZE; byte++)
            memory[byte] = 0;
        user_pt[page_index] =
            page | 0x07 | (nx_enabled ? PTE_NO_EXECUTE : 0);
    }
    task->cr3 = (u64)pml4;

    struct cpu_context *context =
        (struct cpu_context *)(task_kernel_stack_top(task) - sizeof(*context));
    for (unsigned int i = 0; i < sizeof(*context) / sizeof(u64); i++)
        ((u64 *)context)[i] = 0;
    context->rdi = program->argument;
    context->rip = entry_point;
    context->cs = GDT_USER_CODE;
    context->rflags = 0x202;
    context->rsp = USER_STACK;
    context->ss = GDT_USER_DATA;
    task->context = (u64 *)context;
    task->state = TASK_READY;
    serial_write("process: started pid=");
    serial_write_u64((u64)task->pid);
    serial_write("\n");
    return task->pid;
}

int process_exec(const char *name)
{
    int result = process_create(name, 0);
    return result > 0 ? 0 : result;
}

static int user_range_accessible(u64 address, u64 length)
{
    if (!current_task || !current_task->user || length == 0
        || address < USER_BASE || address >= USER_BASE + USER_SIZE
        || length > USER_BASE + USER_SIZE - address)
        return 0;

    unsigned int slot = (unsigned int)(current_task - tasks);
    if (slot == 0 || slot >= MAX_TASKS)
        return 0;

    u64 *user_pt = user_page_tables[slot - 1];
    u64 first_page = (address - USER_BASE) / PAGE_SIZE;
    u64 last_page = (address + length - 1 - USER_BASE) / PAGE_SIZE;

    for (u64 page = first_page; page <= last_page; page++)
        if ((user_pt[page] & 0x05) != 0x05)
            return 0;

    return 1;
}

static u64 user_heap_alloc(struct task *task, unsigned int size)
{
    if (size == 0)
        return 0;

    unsigned int page_count =
        (size + (unsigned int)PAGE_SIZE - 1) / (unsigned int)PAGE_SIZE;
    if (page_count > USER_HEAP_PAGES)
        return 0;

    unsigned int allocation_slot = USER_HEAP_ALLOCATIONS;
    for (unsigned int i = 0; i < USER_HEAP_ALLOCATIONS; i++)
    {
        if (!task->heap_allocations[i].used)
        {
            allocation_slot = i;
            break;
        }
    }
    if (allocation_slot == USER_HEAP_ALLOCATIONS)
        return 0;

    unsigned int slot = (unsigned int)(task - tasks);
    u64 *user_pt = user_page_tables[slot - 1];
    unsigned int first_page = (unsigned int)(USER_HEAP_START - USER_BASE) / PAGE_SIZE;
    unsigned int heap_end_page = first_page + USER_HEAP_PAGES;
    unsigned int run_start = first_page;
    unsigned int run_length = 0;

    for (unsigned int page = first_page; page < heap_end_page; page++)
    {
        if (!(user_pt[page] & 1))
        {
            if (run_length == 0)
                run_start = page;
            if (++run_length == page_count)
                break;
        }
        else
        {
            run_length = 0;
        }
    }
    if (run_length < page_count)
        return 0;

    unsigned int allocated = 0;
    for (; allocated < page_count; allocated++)
    {
        u64 physical_page = physical_page_alloc();
        if (!physical_page)
            break;

        u8 *memory = (u8 *)physical_page;
        for (unsigned int byte = 0; byte < PAGE_SIZE; byte++)
            memory[byte] = 0;

        unsigned int virtual_page = run_start + allocated;
        if (track_task_page(task, physical_page) != 0)
        {
            physical_page_free(physical_page);
            break;
        }
        user_pt[virtual_page] =
            physical_page | 0x07 | (nx_enabled ? PTE_NO_EXECUTE : 0);
    }
    if (allocated != page_count)
    {
        while (allocated)
        {
            unsigned int virtual_page = run_start + --allocated;
            u64 physical_page = user_pt[virtual_page] & PTE_ADDRESS_MASK;
            user_pt[virtual_page] = 0;
            for (unsigned int owned = 0; owned < MAX_TASK_PAGES; owned++)
            {
                if (task->pages[owned] == physical_page)
                {
                    task->pages[owned] = 0;
                    task->page_count--;
                    break;
                }
            }
            physical_page_free(physical_page);
        }
        return 0;
    }

    task->heap_allocations[allocation_slot].first_page = run_start;
    task->heap_allocations[allocation_slot].page_count = page_count;
    task->heap_allocations[allocation_slot].used = 1;
    return USER_BASE + (u64)run_start * PAGE_SIZE;
}

static int user_heap_free(struct task *task, u64 address)
{
    if (address < USER_HEAP_START
        || address >= USER_HEAP_START + USER_HEAP_PAGES * PAGE_SIZE
        || (address & (PAGE_SIZE - 1)))
        return -1;

    unsigned int slot = (unsigned int)(task - tasks);
    u64 *user_pt = user_page_tables[slot - 1];
    unsigned int page = (unsigned int)((address - USER_BASE) / PAGE_SIZE);

    for (unsigned int i = 0; i < USER_HEAP_ALLOCATIONS; i++)
    {
        struct heap_allocation *allocation = &task->heap_allocations[i];
        if (!allocation->used || allocation->first_page != page)
            continue;

        for (unsigned int offset = 0; offset < allocation->page_count; offset++)
        {
            unsigned int virtual_page = page + offset;
            u64 physical_page = user_pt[virtual_page] & PTE_ADDRESS_MASK;
            user_pt[virtual_page] = 0;

            for (unsigned int owned = 0; owned < MAX_TASK_PAGES; owned++)
            {
                if (task->pages[owned] == physical_page)
                {
                    task->pages[owned] = 0;
                    task->page_count--;
                    break;
                }
            }

            if (physical_page_free(physical_page) != 0)
                return -1;
        }
        allocation->used = 0;
        return 0;
    }
    return -1;
}

static int copy_user_string(char *destination, u64 source, unsigned int capacity)
{
    for (unsigned int i = 0; i < capacity; i++)
    {
        if (!user_range_accessible(source + i, 1))
            return -1;
        destination[i] = *(const char *)(source + i);
        if (destination[i] == '\0')
            return i ? 0 : -1;
    }
    destination[capacity - 1] = '\0';
    return -1;
}

static struct task *find_child(int parent_pid, int child_pid)
{
    for (unsigned int i = 1; i < MAX_TASKS; i++)
    {
        if (tasks[i].used && tasks[i].user
            && tasks[i].parent_pid == parent_pid
            && tasks[i].pid == child_pid)
            return &tasks[i];
    }
    return 0;
}

u64 *process_schedule(u64 *context)
{
    return schedule(context, 0);
}

u64 *process_syscall_dispatch(u64 *saved_context)
{
    struct cpu_context *context = (struct cpu_context *)saved_context;

    if (!current_task || !current_task->user)
    {
        context->rax = (u64)-1;
        return saved_context;
    }

    if (context->rax == 0)
    {
        current_task->exit_code = (int)context->rdi;
        return schedule(0, 1);
    }

    if (context->rax == 1)
        return schedule(saved_context, 0);

    if (context->rax == 2)
    {
        u64 address = context->rdi;
        u64 length = context->rsi;
        char text[121];

        if (length == 0 || length > sizeof(text) - 1
            || !user_range_accessible(address, length))
        {
            context->rax = (u64)-1;
            return saved_context;
        }

        for (u64 i = 0; i < length; i++)
            text[i] = ((const char *)address)[i];
        text[length] = '\0';
        print(text, 0x0F);
        context->rax = length;
        return saved_context;
    }

    if (context->rax == 3)
    {
        if (context->rdi > USER_HEAP_PAGES * PAGE_SIZE)
            context->rax = 0;
        else
            context->rax = user_heap_alloc(
                current_task, (unsigned int)context->rdi);
        return saved_context;
    }

    if (context->rax == 4)
    {
        context->rax = user_heap_free(current_task, context->rdi) == 0
            ? 0
            : (u64)-1;
        return saved_context;
    }

    if (context->rax == 5)
    {
        char name[32];
        if (copy_user_string(name, context->rdi, sizeof(name)) != 0)
        {
            context->rax = (u64)-1;
            return saved_context;
        }
        int result = process_create(name, current_task->pid);
        context->rax = result > 0 ? (u64)(u32)result : (u64)-1;
        return saved_context;
    }

    if (context->rax == 6)
    {
        int child_pid = (int)context->rdi;
        struct task *child = find_child(current_task->pid, child_pid);
        if (!child)
        {
            context->rax = (u64)-1;
            return saved_context;
        }
        if (child->state == TASK_ZOMBIE)
        {
            int exit_code = child->exit_code;
            child->used = 0;
            child->state = TASK_FREE;
            context->rax = (u64)exit_code;
            return saved_context;
        }
        current_task->wait_pid = child_pid;
        current_task->state = TASK_BLOCKED;
        return schedule(saved_context, 0);
    }

    if (context->rax == 7)
    {
        u64 duration = context->rdi;
        if (duration == 0)
        {
            context->rax = 0;
            return saved_context;
        }
        current_task->wake_tick = pit_get_ticks() + duration;
        current_task->wait_pid = 0;
        current_task->state = TASK_BLOCKED;
        return schedule(saved_context, 0);
    }

    if (context->rax == 8)
    {
        context->rax = (u64)current_task->pid;
        return saved_context;
    }

    context->rax = (u64)-1;
    return saved_context;
}

void current_process_kill(int code) __attribute__((noreturn));
void current_process_kill(int code)
{
    if (current_task && current_task->user)
        current_task->exit_code = code;

    u64 *context = schedule(0, 1);
    process_restore_context(context);
}

int process_kill(int pid)
{
    for (int i = 0; i < MAX_TASKS; i++){
        if (tasks[i].pid == pid && tasks[i].used){
            if (!tasks[i].user || &tasks[i] == current_task)
                return -1;

            for (unsigned int child = 1; child < MAX_TASKS; child++)
            {
                if (tasks[child].used && tasks[child].user
                    && tasks[child].pid == tasks[i].parent_pid
                    && tasks[child].state == TASK_BLOCKED
                    && tasks[child].wait_pid == tasks[i].pid)
                {
                    ((struct cpu_context *)tasks[child].context)->rax =
                        (u64)-1;
                    tasks[child].wait_pid = 0;
                    tasks[child].state = TASK_READY;
                }
            }
            release_task_pages(&tasks[i]);
            tasks[i].state = TASK_FREE;
            tasks[i].used = 0;
            return 1;
        }
    }
    return 0;
}

int process_reap(int pid, int *exit_code)
{
    if (!exit_code || pid <= 0)
        return PROCESS_REAP_NOT_FOUND;

    for (unsigned int i = 1; i < MAX_TASKS; i++)
    {
        struct task *task = &tasks[i];
        if (!task->used || task->pid != pid || task->parent_pid != 0)
            continue;
        if (task->state != TASK_ZOMBIE)
            return PROCESS_REAP_RUNNING;

        *exit_code = task->exit_code;
        task->used = 0;
        task->state = TASK_FREE;
        task->context = 0;
        return PROCESS_REAP_SUCCESS;
    }
    return PROCESS_REAP_NOT_FOUND;
}

int get_actives_tasks(process_task_info_t *tasks_out, int capacity)
{
    if (capacity < 0 || (!tasks_out && capacity > 0))
        return -1;

    int active_count = 0;
    for (int i = 0; i < MAX_TASKS; i++)
    {
        const struct task *task = &tasks[i];
        if (!task->used)
            continue;

        if (active_count < capacity)
        {
            tasks_out[active_count].pid = task->pid;
            tasks_out[active_count].user = task->user;
            switch (task->state)
            {
            case TASK_RUNNING:
                tasks_out[active_count].state = PROCESS_TASK_RUNNING;
                break;
            case TASK_BLOCKED:
                tasks_out[active_count].state = PROCESS_TASK_BLOCKED;
                break;
            case TASK_ZOMBIE:
                tasks_out[active_count].state = PROCESS_TASK_ZOMBIE;
                break;
            default:
                tasks_out[active_count].state = PROCESS_TASK_READY;
                break;
            }
        }
        active_count++;
    }

    return active_count;
}

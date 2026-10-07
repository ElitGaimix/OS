#define MAX_PROCESSES 4
#define MAX_TASKS PROCESS_MAX_TASKS
#define USER_BASE 0x400000ULL
#define USER_SIZE 0x200000ULL
#define USER_STACK (USER_BASE + 0x200000 - 8)
#define FRAMES_BASE 0x2000000ULL
#define KERNEL_STACK_SIZE 16384
#define USER_PROGRAM_LBA 2048
#define TICKER_PROGRAM_LBA 2056
#define USER_PROGRAM_SECTORS 4

#include <kernel/process/process.h>
#include <kernel/interrupts.h>

extern void print(const char *text, unsigned char color);

struct program
{
    const char *name;
    u32 lba;
    u32 sectors;
    u64 argument;
};

static const struct program programs[] = {
    {"crash", USER_PROGRAM_LBA, USER_PROGRAM_SECTORS, 1},
    {"hello", USER_PROGRAM_LBA, USER_PROGRAM_SECTORS, 0},
    {"ticker", TICKER_PROGRAM_LBA, USER_PROGRAM_SECTORS, 0},
};

enum task_state
{
    TASK_FREE,
    TASK_READY,
    TASK_RUNNING
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

struct task
{
    int pid;
    int used;
    int user;
    enum task_state state;
    int exit_code;
    u64 cr3;
    u64 frame;
    u64 *context;
    u8 kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
};

static int next_pid = 1;
static u64 page_tables[MAX_PROCESSES][3][512] __attribute__((aligned(4096)));
static struct task tasks[MAX_TASKS];
static struct task *current_task;
static u64 kernel_cr3;
static unsigned int current_index;

extern void process_syscall_entry(void);
extern void process_restore_context(u64 *context) __attribute__((noreturn));

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
    return &tasks[after];
}

static u64 *schedule(u64 *context, int task_finished)
{
    unsigned int previous_index = current_index;

    if (current_task)
    {
        if (task_finished)
        {
            current_task->used = 0;
            current_task->state = TASK_FREE;
        }
        else
        {
            current_task->context = context;
            current_task->state = TASK_READY;
        }
    }

    struct task *next = next_ready_task(previous_index);
    next->state = TASK_RUNNING;
    activate_task(next);
    return next->context;
}

void process_init(void)
{
    __asm__ volatile("mov %%cr3, %0" : "=r"(kernel_cr3));

    for (unsigned int i = 0; i < MAX_TASKS; i++)
    {
        tasks[i].used = 0;
        tasks[i].user = 0;
        tasks[i].state = TASK_FREE;
        tasks[i].exit_code = 0;
        tasks[i].cr3 = 0;
        tasks[i].frame = 0;
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

int process_exec(const char *name)
{
    const struct program *program = find_program(name);
    if (!program)
        return -1;

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
    task->frame = FRAMES_BASE + (u64)(slot - 1) * 0x200000;
    task->pid = next_pid;
    next_pid++;

    u8 *memory = (u8 *)task->frame;
    for (u64 i = 0; i < 0x200000; i++)
        memory[i] = 0;
    if (ata_read(program->lba, program->sectors, memory) != 0)
    {
        task->used = 0;
        return -3;
    }

    u64 *pml4 = page_tables[slot - 1][0];
    u64 *pdpt = page_tables[slot - 1][1];
    u64 *pd = page_tables[slot - 1][2];
    for (int i = 0; i < 512; i++)
    {
        pml4[i] = 0;
        pdpt[i] = 0;
        pd[i] = ((u64)i << 21) | 0x83;
    }
    pml4[0] = (u64)pdpt | 0x07;
    pdpt[0] = (u64)pd | 0x07;
    pd[USER_BASE >> 21] = task->frame | 0x87;
    task->cr3 = (u64)pml4;

    struct cpu_context *context =
        (struct cpu_context *)(task_kernel_stack_top(task) - sizeof(*context));
    for (unsigned int i = 0; i < sizeof(*context) / sizeof(u64); i++)
        ((u64 *)context)[i] = 0;
    context->rdi = program->argument;
    context->rip = USER_BASE;
    context->cs = GDT_USER_CODE;
    context->rflags = 0x202;
    context->rsp = USER_STACK;
    context->ss = GDT_USER_DATA;
    task->context = (u64 *)context;
    task->state = TASK_READY;
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
        u64 user_end = USER_BASE + USER_SIZE;
        char text[121];

        if (address < USER_BASE || address >= user_end
            || length == 0 || length > sizeof(text) - 1
            || length > user_end - address)
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
    int result = 0;
    for (int i = 0; i < MAX_TASKS; i++){
        if (tasks[i].pid == pid && tasks[i].used){
            if (tasks[i].user){
                tasks[i].state = TASK_FREE;
                tasks[i].used = 0;
                result = 1;
            }else
                result = -1;
        }
    }
    return result;
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
            tasks_out[active_count].state =
                task->state == TASK_RUNNING
                    ? PROCESS_TASK_RUNNING
                    : PROCESS_TASK_READY;
        }
        active_count++;
    }

    return active_count;
}

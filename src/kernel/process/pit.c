#include <kernel/process/pit.h>
#include <kernel/process/process.h>
#include <kernel/interrupts.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long long u64;

#define PIT_VECTOR 0x20
#define PIT_INPUT_HZ 1193182U
#define PIT_FREQUENCY_HZ 100U
#define PIT_DIVISOR (PIT_INPUT_HZ / PIT_FREQUENCY_HZ)

static volatile u64 ticks;

extern void pit_irq_entry(void);

__asm__(
    ".text\n"
    ".global pit_irq_entry\n"
    ".type pit_irq_entry,@function\n"
    "pit_irq_entry:\n"
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
    "    call pit_schedule_context\n"
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
    ".size pit_irq_entry, .-pit_irq_entry\n");

static inline u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

u64 *pit_schedule_context(u64 *context)
{
    ticks++;
    outb(0x20, 0x20);
    return process_schedule(context);
}

void pit_init(void)
{
    __asm__ volatile("cli" ::: "memory");

    ticks = 0;
    interrupt_register_handler(
        PIT_VECTOR,
        (u64)(unsigned long long)pit_irq_entry);

    outb(0x43, 0x36);
    outb(0x40, (u8)(PIT_DIVISOR & 0xFF));
    outb(0x40, (u8)(PIT_DIVISOR >> 8));

    /* Autorise IRQ0 sans modifier les autres masques du PIC maître. */
    outb(0x21, (u8)(inb(0x21) & ~1U));

    __asm__ volatile("sti" ::: "memory");
}

unsigned long long pit_get_ticks(void)
{
    return ticks;
}

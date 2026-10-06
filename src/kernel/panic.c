#include <kernel/panic.h>
#include <kernel/process/process.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long long u64;

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile u16 *const VGA = (volatile u16 *)0xB8000;

const char *const exception_names[32] = {
    "Division Error",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"};

#define DEFINE_EXCEPTION_NO_ERROR(vector)                    \
    static void exception_##vector(interrupt_frame_t *frame) \
        __attribute__((interrupt));                          \
    static void exception_##vector(interrupt_frame_t *frame) \
    {                                                        \
        kernel_panic_exception(vector, 0, frame);            \
    }

#define DEFINE_EXCEPTION_WITH_ERROR(vector)                                  \
    static void exception_##vector(interrupt_frame_t *frame, u64 error_code) \
        __attribute__((interrupt));                                          \
    static void exception_##vector(interrupt_frame_t *frame, u64 error_code) \
    {                                                                        \
        kernel_panic_exception(vector, error_code, frame);                   \
    }

DEFINE_EXCEPTION_NO_ERROR(0)
DEFINE_EXCEPTION_NO_ERROR(1)
DEFINE_EXCEPTION_NO_ERROR(2)
DEFINE_EXCEPTION_NO_ERROR(3)
DEFINE_EXCEPTION_NO_ERROR(4)
DEFINE_EXCEPTION_NO_ERROR(5)
DEFINE_EXCEPTION_NO_ERROR(6)
DEFINE_EXCEPTION_NO_ERROR(7)
DEFINE_EXCEPTION_WITH_ERROR(8)
DEFINE_EXCEPTION_NO_ERROR(9)
DEFINE_EXCEPTION_WITH_ERROR(10)
DEFINE_EXCEPTION_WITH_ERROR(11)
DEFINE_EXCEPTION_WITH_ERROR(12)
DEFINE_EXCEPTION_WITH_ERROR(13)
DEFINE_EXCEPTION_WITH_ERROR(14)
DEFINE_EXCEPTION_NO_ERROR(15)
DEFINE_EXCEPTION_NO_ERROR(16)
DEFINE_EXCEPTION_WITH_ERROR(17)
DEFINE_EXCEPTION_NO_ERROR(18)
DEFINE_EXCEPTION_NO_ERROR(19)
DEFINE_EXCEPTION_NO_ERROR(20)
DEFINE_EXCEPTION_WITH_ERROR(21)
DEFINE_EXCEPTION_NO_ERROR(22)
DEFINE_EXCEPTION_NO_ERROR(23)
DEFINE_EXCEPTION_NO_ERROR(24)
DEFINE_EXCEPTION_NO_ERROR(25)
DEFINE_EXCEPTION_NO_ERROR(26)
DEFINE_EXCEPTION_NO_ERROR(27)
DEFINE_EXCEPTION_NO_ERROR(28)
DEFINE_EXCEPTION_WITH_ERROR(29)
DEFINE_EXCEPTION_WITH_ERROR(30)
DEFINE_EXCEPTION_NO_ERROR(31)

#define REGISTER_EXCEPTION(vector) \
    interrupt_register_handler(vector, (u64)(unsigned long long)exception_##vector)

void panic_init(void)
{
    interrupts_init();
    REGISTER_EXCEPTION(0);
    REGISTER_EXCEPTION(1);
    REGISTER_EXCEPTION(2);
    REGISTER_EXCEPTION(3);
    REGISTER_EXCEPTION(4);
    REGISTER_EXCEPTION(5);
    REGISTER_EXCEPTION(6);
    REGISTER_EXCEPTION(7);
    REGISTER_EXCEPTION(8);
    REGISTER_EXCEPTION(9);
    REGISTER_EXCEPTION(10);
    REGISTER_EXCEPTION(11);
    REGISTER_EXCEPTION(12);
    REGISTER_EXCEPTION(13);
    REGISTER_EXCEPTION(14);
    REGISTER_EXCEPTION(15);
    REGISTER_EXCEPTION(16);
    REGISTER_EXCEPTION(17);
    REGISTER_EXCEPTION(18);
    REGISTER_EXCEPTION(19);
    REGISTER_EXCEPTION(20);
    REGISTER_EXCEPTION(21);
    REGISTER_EXCEPTION(22);
    REGISTER_EXCEPTION(23);
    REGISTER_EXCEPTION(24);
    REGISTER_EXCEPTION(25);
    REGISTER_EXCEPTION(26);
    REGISTER_EXCEPTION(27);
    REGISTER_EXCEPTION(28);
    REGISTER_EXCEPTION(29);
    REGISTER_EXCEPTION(30);
    REGISTER_EXCEPTION(31);
}

static void write_at(unsigned int row, unsigned int column, const char *text, u8 color)
{
    while (*text && row < VGA_HEIGHT && column < VGA_WIDTH)
    {
        VGA[row * VGA_WIDTH + column++] = (u16)(((u16)color << 8) | (u8)*text++);
    }
}

static void write_hex(unsigned int row, unsigned int column, u64 value)
{
    static const char digits[] = "0123456789ABCDEF";

    write_at(row, column, "0x", 0x0F);
    column += 2;
    for (int shift = 60; shift >= 0; shift -= 4)
    {
        char digit[2] = {digits[(value >> shift) & 0x0F], '\0'};
        write_at(row, column++, digit, 0x0F);
    }
}

void kernel_panic_exception(u8 vector, u64 error_code, interrupt_frame_t *frame)
{
    __asm__ volatile("cli" ::: "memory");
    if ((frame->code_segment & 3) == 3)
        process_kill((vector << 1) + 1);

    int syshalt = 16;
    for (unsigned int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA[i] = (u16)((0x4F << 8) | ' ');

    write_at(2, 2, "KERNEL PANIC", 0x4F);
    write_at(4, 2, "CPU exception", 0x4F);
    write_at(6, 2, "Vector:", 0x0F);
    write_hex(6, 10, vector);
    write_at(8, 2, "Exception:", 0x0F);
    if (vector < 32)
        write_at(8, 13, exception_names[vector], 0x0F);
    if (vector == 14)
    {
        u64 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        write_at(16, 2, "CR2:", 0x0F);
        write_hex(16, 7, cr2);
        u64 cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        u64 *pml4 = (u64 *)(cr3 & ~0xFFFULL);
        u64 *pdpt = (u64 *)(pml4[0] & ~0xFFFULL);
        u64 *pd = (u64 *)(pdpt[0] & ~0xFFFULL);

        write_at(16, 2, "PML4[0]:", 0x0F);
        write_hex(16, 11, pml4[0]);
        write_at(17, 2, "PDPT[0]:", 0x0F);
        write_hex(17, 11, pdpt[0]);
        write_at(18, 2, "PD[2]:", 0x0F);
        write_hex(18, 9, pd[2]);
        syshalt = 20;
    }
    write_at(10, 2, "Error code:", 0x0F);
    write_hex(10, 14, error_code);
    write_at(12, 2, "Instruction pointer:", 0x0F);
    write_hex(12, 23, frame->instruction_pointer);
    write_at(14, 2, "Code segment:", 0x0F);
    write_hex(14, 16, frame->code_segment);
    write_at(syshalt, 2, "System halted.", 0x0F);
    for (;;)
        __asm__ volatile("hlt");
}

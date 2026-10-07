#include <kernel/serial.h>

typedef unsigned char u8;
typedef unsigned short u16;

#define COM1 0x3F8

static int serial_ready;

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

int serial_init(void)
{
    serial_ready = 0;
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);

    for (unsigned int i = 0; i < 100000; i++)
    {
        if (inb(COM1 + 5) & 0x20)
        {
            serial_ready = 1;
            return 0;
        }
    }
    return -1;
}

void serial_write_char(char character)
{
    if (!serial_ready)
        return;

    for (unsigned int i = 0; i < 100000; i++)
    {
        if (inb(COM1 + 5) & 0x20)
        {
            outb(COM1, (u8)character);
            return;
        }
    }
}

void serial_write(const char *text)
{
    if (!text)
        return;

    while (*text)
    {
        if (*text == '\n')
            serial_write_char('\r');
        serial_write_char(*text++);
    }
}

void serial_write_u64(unsigned long long value)
{
    char digits[20];
    unsigned int count = 0;

    do
    {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    while (count)
        serial_write_char(digits[--count]);
}

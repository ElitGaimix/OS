#include <kernel/net/rtl8139.h>
#include <kernel/serial.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139
#define RX_RING_SIZE 8192
#define RX_BUFFER_SIZE (RX_RING_SIZE + 16)
#define MAX_FRAME_SIZE 1536
#define TX_BUFFER_COUNT 4

static u16 io_base;
static u8 mac_address[6];
static u8 receive_ring[RX_BUFFER_SIZE] __attribute__((aligned(256)));
static u8 transmit_buffers[TX_BUFFER_COUNT][MAX_FRAME_SIZE]
    __attribute__((aligned(16)));
static u8 receive_frame[MAX_FRAME_SIZE];
static u32 receive_offset;
static u32 transmit_index;
static u8 transmit_used[TX_BUFFER_COUNT];
static rtl8139_receive_callback_t receive_callback;
static int device_ready;

static inline u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline u32 inl(u16 port)
{
    u32 value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(u16 port, u16 value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outl(u16 port, u32 value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static u8 ring_byte(u32 offset)
{
    return receive_ring[offset % RX_RING_SIZE];
}

int rtl8139_init(rtl8139_receive_callback_t callback)
{
    u32 bar0;
    if (pci_find_device(
            RTL8139_VENDOR,
            RTL8139_DEVICE,
            0,
            0,
            0,
            &bar0) != 0
        || !(bar0 & 1))
        return -1;

    io_base = (u16)(bar0 & ~3U);
    receive_callback = callback;
    device_ready = 0;

    outb(io_base + 0x37, 0x10);
    unsigned int timeout = 1000000;
    while ((inb(io_base + 0x37) & 0x10) && --timeout)
        __asm__ volatile("pause");
    if (!timeout)
    {
        serial_write("rtl8139: reset timed out\n");
        return -1;
    }

    for (unsigned int i = 0; i < 6; i++)
        mac_address[i] = inb(io_base + i);

    receive_offset = 0;
    transmit_index = 0;
    for (unsigned int i = 0; i < TX_BUFFER_COUNT; i++)
        transmit_used[i] = 0;
    outl(io_base + 0x30, (u32)(u64)receive_ring);
    outl(io_base + 0x44, 0x0000008EU);
    outw(io_base + 0x38, 0xFFF0);
    outw(io_base + 0x3C, 0);
    outb(io_base + 0x37, 0x0C);
    device_ready = 1;

    serial_write("rtl8139: initialized mac=");
    for (unsigned int i = 0; i < 6; i++)
    {
        static const char digits[] = "0123456789ABCDEF";
        serial_write_char(digits[mac_address[i] >> 4]);
        serial_write_char(digits[mac_address[i] & 0x0F]);
        if (i != 5)
            serial_write_char(':');
    }
    serial_write("\n");
    return 0;
}

int rtl8139_send(const u8 *frame, u16 length)
{
    if (!device_ready || !frame || length < 14
        || length > MAX_FRAME_SIZE)
        return -1;

    unsigned int index = transmit_index++ % TX_BUFFER_COUNT;
    u16 status_register = io_base + 0x10 + (u16)(index * 4);
    unsigned int timeout = 1000000;
    if (transmit_used[index])
    {
        while (!(inl(status_register) & ((1U << 13) | (1U << 14)))
               && --timeout)
            __asm__ volatile("pause");
        if (!timeout)
            return -1;
    }

    for (u16 i = 0; i < length; i++)
        transmit_buffers[index][i] = frame[i];

    outl(
        io_base + 0x20 + (u16)(index * 4),
        (u32)(u64)transmit_buffers[index]);
    outl(status_register, length);
    transmit_used[index] = 1;
    return 0;
}

void rtl8139_poll(void)
{
    if (!device_ready)
        return;

    for (unsigned int packet_count = 0; packet_count < 16; packet_count++)
    {
        if (inb(io_base + 0x37) & 1)
            return;

        u32 packet_header =
            (u32)ring_byte(receive_offset)
            | ((u32)ring_byte(receive_offset + 1) << 8)
            | ((u32)ring_byte(receive_offset + 2) << 16)
            | ((u32)ring_byte(receive_offset + 3) << 24);
        u16 status = (u16)packet_header;
        u16 packet_length = (u16)(packet_header >> 16);

        if (!(status & 1) || packet_length < 18
            || packet_length > MAX_FRAME_SIZE + 4)
        {
            receive_offset = (receive_offset + 4) & ~(u32)3;
            outw(io_base + 0x38, (u16)(receive_offset - 16));
            continue;
        }

        u16 frame_length = packet_length - 4;
        for (u16 i = 0; i < frame_length; i++)
            receive_frame[i] = ring_byte(receive_offset + 4 + i);

        receive_offset =
            (receive_offset + 4 + packet_length + 3) & ~(u32)3;
        receive_offset %= RX_RING_SIZE;
        outw(io_base + 0x38, (u16)(receive_offset - 16));

        if (receive_callback)
            receive_callback(receive_frame, frame_length);
    }
}

const u8 *rtl8139_mac_address(void)
{
    return device_ready ? mac_address : 0;
}

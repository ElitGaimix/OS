#include <kernel/pci.h>
#include <kernel/serial.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static inline void outl(u16 port, u32 value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline u32 inl(u16 port)
{
    u32 value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static u32 pci_read(u8 bus, u8 device, u8 function, u8 offset)
{
    u32 address = 0x80000000U
        | ((u32)bus << 16)
        | ((u32)device << 11)
        | ((u32)function << 8)
        | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void serial_write_hex16(u16 value)
{
    static const char digits[] = "0123456789ABCDEF";
    for (int shift = 12; shift >= 0; shift -= 4)
        serial_write_char(digits[(value >> shift) & 0x0F]);
}

int pci_scan(void)
{
    int found = 0;
    for (unsigned int bus = 0; bus < 256; bus++)
    {
        for (unsigned int device = 0; device < 32; device++)
        {
            u32 identity =
                pci_read((u8)bus, (u8)device, 0, 0);
            if ((identity & 0xFFFF) == 0xFFFF)
                continue;

            u32 header = pci_read((u8)bus, (u8)device, 0, 0x0C);
            unsigned int function_count =
                (header & 0x00800000U) ? 8 : 1;

            for (unsigned int function = 0; function < function_count; function++)
            {
                identity = pci_read(
                    (u8)bus,
                    (u8)device,
                    (u8)function,
                    0);
                u16 vendor = (u16)identity;
                if (vendor == 0xFFFF)
                    continue;
                u16 product = (u16)(identity >> 16);

                serial_write("pci: ");
                serial_write_hex16((u16)bus);
                serial_write_char(':');
                serial_write_hex16((u16)device);
                serial_write_char('.');
                serial_write_hex16((u16)function);
                serial_write(" vendor=");
                serial_write_hex16(vendor);
                serial_write(" device=");
                serial_write_hex16(product);
                serial_write("\n");
                found++;
            }
        }
    }

    serial_write("pci: devices=");
    serial_write_u64((unsigned long long)found);
    serial_write("\n");
    return found;
}

int pci_find_device(
    u16 wanted_vendor,
    u16 wanted_product,
    u8 *bus_out,
    u8 *device_out,
    u8 *function_out,
    u32 *bar0_out)
{
    for (unsigned int bus = 0; bus < 256; bus++)
    {
        for (unsigned int device = 0; device < 32; device++)
        {
            u32 header = pci_read((u8)bus, (u8)device, 0, 0x0C);
            unsigned int function_count =
                (header & 0x00800000U) ? 8 : 1;

            for (unsigned int function = 0; function < function_count; function++)
            {
                u32 identity = pci_read(
                    (u8)bus,
                    (u8)device,
                    (u8)function,
                    0);
                if ((u16)identity != wanted_vendor
                    || (u16)(identity >> 16) != wanted_product)
                    continue;

                u32 bar = pci_read(
                    (u8)bus,
                    (u8)device,
                    (u8)function,
                    0x10);
                u32 command_status = pci_read(
                    (u8)bus,
                    (u8)device,
                    (u8)function,
                    0x04);
                outl(
                    PCI_CONFIG_ADDRESS,
                    0x80000000U | ((u32)bus << 16)
                        | ((u32)device << 11)
                        | ((u32)function << 8) | 0x04);
                outl(
                    PCI_CONFIG_DATA,
                    (command_status & 0xFFFFU) | 0x00000005U);

                if (bus_out)
                    *bus_out = (u8)bus;
                if (device_out)
                    *device_out = (u8)device;
                if (function_out)
                    *function_out = (u8)function;
                if (bar0_out)
                    *bar0_out = bar;
                return 0;
            }
        }
    }
    return -1;
}

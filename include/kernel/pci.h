#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

typedef unsigned char pci_u8;
typedef unsigned short pci_u16;
typedef unsigned int pci_u32;

int pci_scan(void);
int pci_find_device(
    pci_u16 vendor,
    pci_u16 device,
    pci_u8 *bus_out,
    pci_u8 *device_out,
    pci_u8 *function_out,
    pci_u32 *bar0_out);

#endif

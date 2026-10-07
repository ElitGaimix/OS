#ifndef KERNEL_NET_RTL8139_H
#define KERNEL_NET_RTL8139_H

#include <kernel/pci.h>

typedef void (*rtl8139_receive_callback_t)(const unsigned char *frame,
                                            unsigned short length);

int rtl8139_init(rtl8139_receive_callback_t callback);
int rtl8139_send(const unsigned char *frame, unsigned short length);
void rtl8139_poll(void);
const unsigned char *rtl8139_mac_address(void);

#endif

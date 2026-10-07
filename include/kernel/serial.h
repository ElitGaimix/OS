#ifndef KERNEL_SERIAL_H
#define KERNEL_SERIAL_H

int serial_init(void);
void serial_write_char(char character);
void serial_write(const char *text);
void serial_write_u64(unsigned long long value);

#endif

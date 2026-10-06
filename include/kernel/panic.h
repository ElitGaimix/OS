#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <kernel/interrupts.h>
#include <kernel/kshell.h>

void panic_init(void);
void kernel_panic_exception(
	unsigned char vector,
	unsigned long long error_code,
	interrupt_frame_t *frame
) __attribute__((noreturn));

#endif
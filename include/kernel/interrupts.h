#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

typedef struct {
	unsigned long long instruction_pointer;
	unsigned long long code_segment;
	unsigned long long flags;
} interrupt_frame_t;

void interrupts_init(void);
void interrupt_register_handler(unsigned char vector, unsigned long long address);

#endif
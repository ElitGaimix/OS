#ifndef KERNEL_DRIVERS_TIMER_PIT_H
#define KERNEL_DRIVERS_TIMER_PIT_H

void pit_init(void);
unsigned long long pit_get_ticks(void);

#endif

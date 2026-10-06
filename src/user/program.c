#include <user/syscall.h>

__attribute__((section(".text.entry")))
void user_main(user_syscall_u64 mode)
{
    if (mode == 1)
    {
        volatile int dividend = 1;
        volatile int divisor = 0;
        volatile int result = dividend / divisor;
        (void)result;
    }

    volatile user_syscall_u64 counter = 0;
    while (counter < 300000000ULL)
        counter++;
    user_exit(0);
}

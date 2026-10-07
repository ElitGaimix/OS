#include <user/syscall.h>

__attribute__((section(".text.entry")))
void user_main(user_syscall_u64 mode)
{
    if (mode == 2)
    {
        int result = user_print((const char *)0x401000, 8);
        user_exit(result == -1 ? 0 : 2);
    }
    if (mode == 3)
    {
        *(volatile unsigned char *)0x400000 = 0;
        user_exit(3);
    }
    if (mode == 4)
    {
        *(volatile unsigned char *)0x100000 = 0;
        user_exit(4);
    }
    if (mode == 5)
    {
        volatile unsigned long long *memory =
            user_malloc(sizeof(*memory) * 512);
        if (!memory)
            user_exit(5);
        for (unsigned long long i = 0; i < 512; i++)
            memory[i] = i * 3;
        if (memory[511] != 1533 || user_free((void *)memory) != 0)
            user_exit(6);
        user_exit(0);
    }
    if (mode == 6)
    {
        int child = user_spawn("quick");
        if (child <= 0)
            user_exit(7);
        user_exit(user_wait(child) == 42 ? 0 : 8);
    }
    if (mode == 7)
    {
        user_sleep(2);
        user_exit(42);
    }
    if (mode == 8)
    {
        unsigned char *code = user_malloc(4096);
        if (!code)
            user_exit(9);
        code[0] = 0xC3;
        void (*execute_heap)(void) = (void (*)(void))code;
        execute_heap();
        user_exit(10);
    }

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

#include <user/syscall.h>

__attribute__((section(".text.entry")))
void user_main(void)
{
    for (user_syscall_u64 message = 1; message <= 20; message++)
    {
        char line[] = "ticker: message 0 / 10\n";
        line[16] = (char)('0' + message);
        user_print(line, sizeof(line) - 1);

        for (volatile user_syscall_u64 delay = 0; delay < 3000000ULL; delay++)
            __asm__ volatile("pause");
    }

    user_exit(0);
}

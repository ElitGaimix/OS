#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

typedef unsigned long long user_syscall_u64;

static inline user_syscall_u64 user_syscall(
    user_syscall_u64 number,
    user_syscall_u64 argument0,
    user_syscall_u64 argument1)
{
    register user_syscall_u64 result __asm__("rax") = number;
    register user_syscall_u64 first_argument __asm__("rdi") = argument0;
    register user_syscall_u64 second_argument __asm__("rsi") = argument1;

    __asm__ volatile(
        "int $0x80"
        : "+a"(result)
        : "D"(first_argument), "S"(second_argument)
        : "memory", "cc");
    return result;
}

static inline void user_exit(user_syscall_u64 code)
{
    user_syscall(0, code, 0);
    for (;;)
        __asm__ volatile("pause");
}

static inline int user_print(const char *text, user_syscall_u64 length)
{
    return (int)user_syscall(2, (user_syscall_u64)text, length);
}

#endif

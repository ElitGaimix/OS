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

static inline void *user_malloc(user_syscall_u64 size)
{
    return (void *)user_syscall(3, size, 0);
}

static inline int user_free(void *pointer)
{
    return (int)user_syscall(4, (user_syscall_u64)pointer, 0);
}

static inline int user_spawn(const char *name)
{
    return (int)user_syscall(5, (user_syscall_u64)name, 0);
}

static inline int user_wait(int pid)
{
    return (int)user_syscall(6, (user_syscall_u64)pid, 0);
}

static inline void user_sleep(user_syscall_u64 ticks)
{
    user_syscall(7, ticks, 0);
}

static inline int user_getpid(void)
{
    return (int)user_syscall(8, 0, 0);
}

#endif

#include <kernel/drivers/disk/harddrive.h>
#include <kernel/utils/string.h>
#include <kernel/gdt.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

int process_exec(const char *name);
void process_init(void);
u64 *process_schedule(u64 *context);
void process_kill(int code) __attribute__((noreturn));
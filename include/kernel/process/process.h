#include <kernel/drivers/disk/harddrive.h>
#include <kernel/utils/string.h>
#include <kernel/gdt.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

int process_exec(const char *name);
void process_kill(int code);
void process_init(void);
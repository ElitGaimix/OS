#ifndef KERNEL_PROCESS_PROCESS_H
#define KERNEL_PROCESS_PROCESS_H

#include <e820.h>
#include <kernel/drivers/disk/harddrive.h>
#include <kernel/utils/string.h>
#include <kernel/gdt.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define PROCESS_MAX_TASKS 5

typedef enum
{
    PROCESS_TASK_READY,
    PROCESS_TASK_RUNNING,
    PROCESS_TASK_BLOCKED,
    PROCESS_TASK_ZOMBIE
} process_task_state_t;

typedef struct
{
    int pid;
    int user;
    process_task_state_t state;
} process_task_info_t;

#define PROCESS_REAP_SUCCESS 0
#define PROCESS_REAP_RUNNING 1
#define PROCESS_REAP_NOT_FOUND 2

int process_exec(const char *name);
void process_init(const e820_entry_t *memory_map, e820_u32_t entry_count);
u64 *process_schedule(u64 *context);
void current_process_kill(int code) __attribute__((noreturn));
int process_kill(int pid);
int process_reap(int pid, int *exit_code);
/* Returns the total active count and copies up to capacity task snapshots. */
int get_actives_tasks(process_task_info_t *tasks_out, int capacity);

#endif

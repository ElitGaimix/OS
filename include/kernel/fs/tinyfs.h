#ifndef KERNEL_FS_TINYFS_H
#define KERNEL_FS_TINYFS_H

#include <kernel/drivers/disk/harddrive.h>

typedef struct
{
    u32 start_lba;
    u32 size;
} tinyfs_file_t;

typedef struct
{
    char name[16];
    u32 size;
} tinyfs_dirent_t;

int tinyfs_init(void);
int tinyfs_open(const char *name, tinyfs_file_t *file);
int tinyfs_list(tinyfs_dirent_t *entries, u32 capacity);
int tinyfs_read(
    const tinyfs_file_t *file,
    u32 offset,
    u32 length,
    void *destination);
int tinyfs_write(const char *name, const void *source, u32 size);
int tinyfs_remove(const char *name);

#endif

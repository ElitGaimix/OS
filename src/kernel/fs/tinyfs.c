#include <kernel/fs/tinyfs.h>
#include <kernel/serial.h>

#define TINYFS_LBA 512
#define TINYFS_MAGIC 0x31534654U
#define TINYFS_VERSION 1
#define TINYFS_MAX_FILES 16
#define TINYFS_NAME_SIZE 16
#define TINYFS_IMAGE_END_LBA 2048

typedef struct __attribute__((packed))
{
    char name[TINYFS_NAME_SIZE];
    u32 start_lba;
    u32 size;
} tinyfs_entry_t;

typedef struct __attribute__((packed))
{
    u32 magic;
    u32 version;
    u32 file_count;
    tinyfs_entry_t entries[TINYFS_MAX_FILES];
    u8 reserved[512 - 12 - TINYFS_MAX_FILES * sizeof(tinyfs_entry_t)];
} tinyfs_header_t;

static tinyfs_header_t filesystem;
static int filesystem_ready;
static u8 sector_buffer[512] __attribute__((aligned(16)));

static int strings_equal(const char *left, const char *right)
{
    while (*left && *right && *left == *right)
    {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int entry_name_valid(const tinyfs_entry_t *entry)
{
    int terminated = 0;
    for (u32 i = 0; i < TINYFS_NAME_SIZE; i++)
    {
        if (!entry->name[i])
        {
            terminated = 1;
            break;
        }
        if ((u8)entry->name[i] < 0x20 || (u8)entry->name[i] > 0x7E)
            return 0;
    }
    return terminated && entry->name[0] != '\0';
}

static void copy_bytes(void *destination, const void *source, u32 length)
{
    u8 *output = destination;
    const u8 *input = source;
    for (u32 i = 0; i < length; i++)
        output[i] = input[i];
}

int tinyfs_init(void)
{
    filesystem_ready = 0;
    if (ata_read(TINYFS_LBA, 1, &filesystem) != 0)
    {
        serial_write("tinyfs: failed to read filesystem header\n");
        return -1;
    }
    if (filesystem.magic != TINYFS_MAGIC
        || filesystem.version != TINYFS_VERSION
        || filesystem.file_count > TINYFS_MAX_FILES)
    {
        serial_write("tinyfs: invalid filesystem header\n");
        return -1;
    }

    for (u32 i = 0; i < filesystem.file_count; i++)
    {
        tinyfs_entry_t *entry = &filesystem.entries[i];
        u32 sectors = entry->size
            ? ((entry->size - 1U) / 512U) + 1U
            : 0;
        if (!entry_name_valid(entry) || entry->size == 0
            || entry->start_lba < TINYFS_LBA + 1
            || entry->start_lba >= TINYFS_IMAGE_END_LBA
            || sectors > TINYFS_IMAGE_END_LBA - entry->start_lba
            )
        {
            serial_write("tinyfs: invalid file entry\n");
            return -1;
        }

        for (u32 previous = 0; previous < i; previous++)
        {
            tinyfs_entry_t *other = &filesystem.entries[previous];
            u32 other_sectors = ((other->size - 1U) / 512U) + 1U;
            u32 end_lba = entry->start_lba + sectors;
            u32 other_end_lba = other->start_lba + other_sectors;
            if (strings_equal(entry->name, other->name)
                || (entry->start_lba < other_end_lba
                    && other->start_lba < end_lba))
            {
                serial_write("tinyfs: duplicate name or overlapping files\n");
                return -1;
            }
        }
    }

    filesystem_ready = 1;
    serial_write("tinyfs: mounted read-write\n");
    return 0;
}

int tinyfs_open(const char *name, tinyfs_file_t *file)
{
    if (!filesystem_ready || !name || !file)
        return -1;

    for (u32 i = 0; i < filesystem.file_count; i++)
    {
        tinyfs_entry_t *entry = &filesystem.entries[i];
        if (!strings_equal(entry->name, name))
            continue;
        file->start_lba = entry->start_lba;
        file->size = entry->size;
        return 0;
    }
    return -1;
}

int tinyfs_list(tinyfs_dirent_t *entries, u32 capacity)
{
    if (!filesystem_ready || (!entries && capacity))
        return -1;
    if (capacity < filesystem.file_count)
        return -1;

    for (u32 i = 0; i < filesystem.file_count; i++)
    {
        for (u32 j = 0; j < TINYFS_NAME_SIZE; j++)
            entries[i].name[j] = filesystem.entries[i].name[j];
        entries[i].size = filesystem.entries[i].size;
    }
    return (int)filesystem.file_count;
}

int tinyfs_read(
    const tinyfs_file_t *file,
    u32 offset,
    u32 length,
    void *destination)
{
    if (!filesystem_ready || !file || (!destination && length)
        || offset > file->size || length > file->size - offset)
        return -1;
    u32 sectors = file->size
        ? ((file->size - 1U) / 512U) + 1U
        : 0;
    if (!file->size || file->start_lba < TINYFS_LBA + 1
        || file->start_lba >= TINYFS_IMAGE_END_LBA
        || sectors > TINYFS_IMAGE_END_LBA - file->start_lba)
        return -1;

    u8 *output = destination;
    while (length)
    {
        u32 sector_index = offset / 512U;
        u32 sector_offset = offset % 512U;
        u32 amount = 512U - sector_offset;
        if (amount > length)
            amount = length;

        if (ata_read(file->start_lba + sector_index, 1, sector_buffer) != 0)
            return -1;
        for (u32 i = 0; i < amount; i++)
            output[i] = sector_buffer[sector_offset + i];

        offset += amount;
        output += amount;
        length -= amount;
    }
    return 0;
}

static int find_entry(const char *name)
{
    for (u32 i = 0; i < filesystem.file_count; i++)
        if (strings_equal(filesystem.entries[i].name, name))
            return (int)i;
    return -1;
}

static int extent_is_free(u32 start_lba, u32 sectors)
{
    u32 end_lba = start_lba + sectors;
    for (u32 i = 0; i < filesystem.file_count; i++)
    {
        tinyfs_entry_t *entry = &filesystem.entries[i];
        u32 entry_sectors = ((entry->size - 1U) / 512U) + 1U;
        u32 entry_end_lba = entry->start_lba + entry_sectors;
        if (start_lba < entry_end_lba && entry->start_lba < end_lba)
            return 0;
    }
    return 1;
}

int tinyfs_write(const char *name, const void *source, u32 size)
{
    if (!filesystem_ready || !name || !source || !size
        || size > (TINYFS_IMAGE_END_LBA - TINYFS_LBA - 1U) * 512U)
        return -1;

    tinyfs_entry_t new_entry = {{0}, 0, size};
    u32 name_length = 0;
    while (name[name_length])
    {
        if (name_length >= TINYFS_NAME_SIZE - 1
            || (u8)name[name_length] < 0x20
            || (u8)name[name_length] > 0x7E)
            return -1;
        new_entry.name[name_length] = name[name_length];
        name_length++;
    }
    if (!name_length)
        return -1;

    int existing = find_entry(name);
    if (existing < 0 && filesystem.file_count == TINYFS_MAX_FILES)
        return -1;

    u32 sectors = ((size - 1U) / 512U) + 1U;
    u32 limit = TINYFS_IMAGE_END_LBA;
    u32 selected_lba = 0;
    for (u32 candidate = TINYFS_LBA + 1;
         candidate <= limit && sectors <= limit - candidate;
         candidate++)
    {
        if (extent_is_free(candidate, sectors))
        {
            selected_lba = candidate;
            break;
        }
    }
    if (!selected_lba)
        return -1;

    const u8 *input = source;
    u8 sector[512];
    u8 verify_sector[512];
    for (u32 i = 0; i < sectors; i++)
    {
        u32 offset = i * 512U;
        u32 amount = size - offset;
        if (amount > sizeof(sector))
            amount = sizeof(sector);
        for (u32 byte = 0; byte < sizeof(sector); byte++)
            sector[byte] = byte < amount ? input[offset + byte] : 0;
        if (ata_write(selected_lba + i, 1, sector) != 0)
        {
            serial_write("tinyfs: data write failed\n");
            return -1;
        }
        if (ata_read(selected_lba + i, 1, verify_sector) != 0)
        {
            serial_write("tinyfs: data verification read failed\n");
            return -1;
        }
        for (u32 byte = 0; byte < sizeof(sector); byte++)
        {
            if (verify_sector[byte] != sector[byte])
            {
                serial_write("tinyfs: data verification mismatch\n");
                return -1;
            }
        }
    }

    new_entry.start_lba = selected_lba;
    tinyfs_header_t previous;
    copy_bytes(&previous, &filesystem, sizeof(previous));
    if (existing >= 0)
    {
        copy_bytes(
            &filesystem.entries[existing],
            &new_entry,
            sizeof(new_entry));
    }
    else
    {
        copy_bytes(
            &filesystem.entries[filesystem.file_count],
            &new_entry,
            sizeof(new_entry));
        filesystem.file_count++;
    }
    if (ata_write(TINYFS_LBA, 1, &filesystem) != 0)
    {
        copy_bytes(&filesystem, &previous, sizeof(filesystem));
        serial_write("tinyfs: directory commit failed\n");
        return -1;
    }
    serial_write("tinyfs: file stored\n");
    return 0;
}

int tinyfs_remove(const char *name)
{
    if (!filesystem_ready || !name)
        return -1;
    int index = find_entry(name);
    if (index < 0)
    {
        serial_write("tinyfs: remove target not found\n");
        return -1;
    }

    tinyfs_header_t previous;
    copy_bytes(&previous, &filesystem, sizeof(previous));
    for (u32 i = (u32)index; i + 1 < filesystem.file_count; i++)
    {
        copy_bytes(
            &filesystem.entries[i],
            &filesystem.entries[i + 1],
            sizeof(filesystem.entries[i]));
    }
    filesystem.file_count--;
    for (u32 i = filesystem.file_count; i < TINYFS_MAX_FILES; i++)
    {
        for (u32 byte = 0; byte < sizeof(filesystem.entries[i]); byte++)
            ((u8 *)&filesystem.entries[i])[byte] = 0;
    }

    if (ata_write(TINYFS_LBA, 1, &filesystem) != 0)
    {
        copy_bytes(&filesystem, &previous, sizeof(filesystem));
        serial_write("tinyfs: delete commit failed\n");
        return -1;
    }
    serial_write("tinyfs: file removed\n");
    return 0;
}

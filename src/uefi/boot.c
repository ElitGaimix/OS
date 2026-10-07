#include <e820.h>

typedef unsigned long long efi_uintn_t;
typedef unsigned long long efi_status_t;
typedef void *efi_handle_t;
typedef unsigned short efi_char16_t;

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_ALLOCATE_MAX_ADDRESS 1
#define EFI_ALLOCATE_ADDRESS 2
#define EFI_LOADER_DATA 2
#define EFI_FILE_MODE_READ 1
#define EFI_PAGE_SIZE 4096ULL
#define KERNEL_ADDRESS 0x100000ULL
#define KERNEL_RESERVED_SIZE (2ULL * 1024 * 1024)
#define MAX_E820_ENTRIES 512
#define MEMORY_MAP_CAPACITY (128ULL * 1024)

typedef struct
{
    unsigned int data1;
    unsigned short data2;
    unsigned short data3;
    unsigned char data4[8];
} efi_guid_t;

typedef struct
{
    unsigned long long signature;
    unsigned int revision;
    unsigned int header_size;
    unsigned int crc32;
    unsigned int reserved;
    efi_char16_t *firmware_vendor;
    unsigned int firmware_revision;
    unsigned int padding;
    efi_handle_t console_in_handle;
    void *console_in;
    efi_handle_t console_out_handle;
    struct efi_text_output_protocol *console_out;
    efi_handle_t standard_error_handle;
    void *standard_error;
    void *runtime_services;
    void *boot_services;
} efi_system_table_t;

typedef struct efi_text_output_protocol
{
    unsigned long long revision;
    efi_status_t(EFIAPI *reset)(struct efi_text_output_protocol *, unsigned char);
    efi_status_t(EFIAPI *output_string)(
        struct efi_text_output_protocol *, efi_char16_t *);
} efi_text_output_protocol_t;

typedef struct
{
    unsigned int type;
    unsigned int padding;
    unsigned long long physical_start;
    unsigned long long virtual_start;
    unsigned long long pages;
    unsigned long long attributes;
} efi_memory_descriptor_t;

typedef struct efi_file efi_file_t;
struct efi_file
{
    unsigned long long revision;
    efi_status_t(EFIAPI *open)(
        efi_file_t *,
        efi_file_t **,
        efi_char16_t *,
        unsigned long long,
        unsigned long long);
    efi_status_t(EFIAPI *close)(efi_file_t *);
    void *delete_file;
    efi_status_t(EFIAPI *read)(efi_file_t *, efi_uintn_t *, void *);
    void *write;
    void *get_position;
    void *set_position;
    efi_status_t(EFIAPI *get_info)(
        efi_file_t *, efi_guid_t *, efi_uintn_t *, void *);
};

typedef struct
{
    unsigned long long revision;
    efi_status_t(EFIAPI *open_volume)(void *, efi_file_t **);
} efi_simple_file_system_t;

typedef struct
{
    efi_uintn_t size;
    unsigned long long file_size;
    unsigned long long physical_size;
    unsigned char remainder[56];
} efi_file_info_t;

typedef efi_status_t(EFIAPI *efi_handle_protocol_t)(
    efi_handle_t, efi_guid_t *, void **);
typedef efi_status_t(EFIAPI *efi_allocate_pages_t)(
    unsigned int, unsigned int, efi_uintn_t, unsigned long long *);
typedef efi_status_t(EFIAPI *efi_allocate_pool_t)(
    unsigned int, efi_uintn_t, void **);
typedef efi_status_t(EFIAPI *efi_get_memory_map_t)(
    efi_uintn_t *,
    efi_memory_descriptor_t *,
    unsigned long long *,
    efi_uintn_t *,
    unsigned int *);
typedef efi_status_t(EFIAPI *efi_exit_boot_services_t)(
    efi_handle_t, unsigned long long);

typedef struct
{
    unsigned int type;
    unsigned int padding;
    unsigned long long physical_start;
    unsigned long long virtual_start;
    unsigned long long pages;
    unsigned long long attributes;
} efi_memory_descriptor_layout_check_t;

static const efi_guid_t loaded_image_guid = {
    0x5B1B31A1, 0x9562, 0x11D2,
    {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
static const efi_guid_t simple_fs_guid = {
    0x964E5B22, 0x6459, 0x11D2,
    {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
static const efi_guid_t file_info_guid = {
    0x09576E92, 0x6D3F, 0x11D2,
    {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

typedef struct
{
    unsigned int revision;
    efi_handle_t parent_handle;
    efi_system_table_t *system_table;
    efi_handle_t device_handle;
    void *file_path;
    void *reserved;
    unsigned int load_options_size;
    unsigned int padding;
    void *load_options;
    void *image_base;
    unsigned long long image_size;
    unsigned int image_code_type;
    unsigned int image_data_type;
    void *unload;
} efi_loaded_image_t;

static e820_entry_t e820_entries[MAX_E820_ENTRIES];
static e820_entry_t * volatile e820_anchor = e820_entries;
static unsigned int e820_count;
static volatile unsigned short *const uart = (unsigned short *)0x3F8;

static void console_write(
    efi_system_table_t *system_table,
    const char *text)
{
    efi_char16_t buffer[128];
    unsigned int length = 0;
    if (!system_table->console_out
        || !system_table->console_out->output_string)
        return;

    while (*text && length + 2 < sizeof(buffer) / sizeof(buffer[0]))
    {
        if (*text == '\n')
            buffer[length++] = '\r';
        buffer[length++] = (unsigned char)*text++;
    }
    buffer[length] = 0;
    system_table->console_out->output_string(
        system_table->console_out,
        buffer);
}

static void serial_write(const char *text)
{
    while (*text)
    {
        unsigned int timeout = 100000;
        while (!((*(volatile unsigned char *)((unsigned long long)uart + 5))
                 & 0x20)
               && --timeout)
            __asm__ volatile("pause");
        if (!timeout)
            return;
        *(volatile unsigned char *)uart = (unsigned char)*text++;
    }
}

static void zero_memory(void *address, efi_uintn_t size)
{
    unsigned char *bytes = address;
    for (efi_uintn_t i = 0; i < size; i++)
        bytes[i] = 0;
}

static void *boot_service(
    efi_system_table_t *system_table,
    unsigned int index)
{
    return *(void **)((unsigned char *)system_table->boot_services
        + 24 + index * sizeof(void *));
}

static int efi_failed(efi_status_t status)
{
    return (long long)status < 0;
}

static int load_kernel(
    efi_system_table_t *system_table,
    efi_handle_t image_handle,
    efi_allocate_pages_t allocate_pages,
    efi_allocate_pool_t allocate_pool)
{
    efi_handle_protocol_t handle_protocol =
        (efi_handle_protocol_t)boot_service(system_table, 16);
    void *loaded_image_protocol = 0;
    if (efi_failed(handle_protocol(
            image_handle,
            (efi_guid_t *)&loaded_image_guid,
            &loaded_image_protocol)))
        return -1;

    efi_loaded_image_t *loaded_image = loaded_image_protocol;
    void *filesystem_protocol = 0;
    if (efi_failed(handle_protocol(
            loaded_image->device_handle,
            (efi_guid_t *)&simple_fs_guid,
            &filesystem_protocol)))
        return -1;

    efi_simple_file_system_t *filesystem = filesystem_protocol;
    efi_file_t *root = 0;
    if (efi_failed(filesystem->open_volume(filesystem, &root)) || !root)
        return -1;

    static efi_char16_t filename[] = {
        '\\','E','F','I','\\','B','O','O','T','\\',
        'K','E','R','N','E','L','.','B','I','N',0};
    efi_file_t *kernel_file = 0;
    efi_status_t status = root->open(
        root,
        &kernel_file,
        filename,
        EFI_FILE_MODE_READ,
        0);
    root->close(root);
    if (efi_failed(status) || !kernel_file)
        return -1;

    unsigned char file_info_storage[256];
    efi_uintn_t info_size = sizeof(file_info_storage);
    status = kernel_file->get_info(
        kernel_file,
        (efi_guid_t *)&file_info_guid,
        &info_size,
        file_info_storage);
    if (efi_failed(status) || info_size < sizeof(efi_file_info_t))
    {
        kernel_file->close(kernel_file);
        return -1;
    }

    efi_file_info_t *file_info = (efi_file_info_t *)file_info_storage;
    if (!file_info->file_size || file_info->file_size > KERNEL_RESERVED_SIZE)
    {
        kernel_file->close(kernel_file);
        return -1;
    }

    unsigned long long kernel_address = KERNEL_ADDRESS;
    efi_uintn_t kernel_pages =
        (KERNEL_RESERVED_SIZE + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    status = allocate_pages(
        EFI_ALLOCATE_ADDRESS,
        EFI_LOADER_DATA,
        kernel_pages,
        &kernel_address);
    if (efi_failed(status) || kernel_address != KERNEL_ADDRESS)
    {
        kernel_file->close(kernel_file);
        return -1;
    }
    zero_memory((void *)kernel_address, KERNEL_RESERVED_SIZE);

    efi_uintn_t bytes_read = file_info->file_size;
    status = kernel_file->read(
        kernel_file,
        &bytes_read,
        (void *)kernel_address);
    kernel_file->close(kernel_file);
    if (efi_failed(status) || bytes_read != file_info->file_size)
        return -1;

    void *memory_map = 0;
    if (efi_failed(allocate_pool(
            EFI_LOADER_DATA,
            MEMORY_MAP_CAPACITY,
            &memory_map)))
        return -1;

    efi_allocate_pages_t allocate_pages_for_map = allocate_pages;
    unsigned long long maximum_address = 0xFFFFFFFFULL;
    unsigned long long table_address = maximum_address;
    if (efi_failed(allocate_pages_for_map(
            EFI_ALLOCATE_MAX_ADDRESS,
            EFI_LOADER_DATA,
            6,
            &table_address)))
        return -1;

    unsigned long long *pml4 = (unsigned long long *)table_address;
    unsigned long long *pdpt = pml4 + 512;
    unsigned long long *page_directories = pdpt + 512;
    zero_memory((void *)table_address, 6 * EFI_PAGE_SIZE);
    pml4[0] = ((unsigned long long)pdpt) | 3;
    for (unsigned int directory = 0; directory < 4; directory++)
    {
        pdpt[directory] =
            ((unsigned long long)(page_directories + directory * 512)) | 3;
        for (unsigned int page = 0; page < 512; page++)
        {
            unsigned long long physical =
                ((unsigned long long)directory * 512 + page) << 21;
            page_directories[directory * 512 + page] = physical | 0x83;
        }
    }

    unsigned long long stack_address = 0xFFFFFFFFULL;
    if (efi_failed(allocate_pages(
            EFI_ALLOCATE_MAX_ADDRESS,
            EFI_LOADER_DATA,
            16,
            &stack_address)))
        return -1;
    unsigned long long stack_top =
        stack_address + 16 * EFI_PAGE_SIZE - sizeof(unsigned long long);

    efi_uintn_t map_size = MEMORY_MAP_CAPACITY;
    unsigned long long map_key = 0;
    efi_uintn_t descriptor_size = 0;
    unsigned int descriptor_version = 0;
    efi_get_memory_map_t get_memory_map =
        (efi_get_memory_map_t)boot_service(system_table, 4);
    efi_exit_boot_services_t exit_boot_services =
        (efi_exit_boot_services_t)boot_service(system_table, 26);

    status = get_memory_map(
        &map_size,
        memory_map,
        &map_key,
        &descriptor_size,
        &descriptor_version);
    if (efi_failed(status) || descriptor_size < sizeof(efi_memory_descriptor_layout_check_t))
        return -1;

    e820_count = 0;
    for (efi_uintn_t offset = 0;
         offset + descriptor_size <= map_size
             && e820_count < MAX_E820_ENTRIES;
         offset += descriptor_size)
    {
        efi_memory_descriptor_t *descriptor =
            (efi_memory_descriptor_t *)((unsigned char *)memory_map + offset);
        unsigned long long length = descriptor->pages * EFI_PAGE_SIZE;
        e820_entry_t *entry = &e820_entries[e820_count++];
        entry->base_low = (unsigned int)descriptor->physical_start;
        entry->base_high = (unsigned int)(descriptor->physical_start >> 32);
        entry->length_low = (unsigned int)length;
        entry->length_high = (unsigned int)(length >> 32);
        entry->type = descriptor->type == 7 ? 1 : 2;
        entry->attributes = 1;
    }
    if (!e820_count)
        return -1;

    status = exit_boot_services(image_handle, map_key);
    if (efi_failed(status))
        return -1;

    unsigned int efer_low, efer_high;
    __asm__ volatile(
        "rdmsr"
        : "=a"(efer_low), "=d"(efer_high)
        : "c"(0xC0000080));
    efer_low |= 1U << 11;
    __asm__ volatile(
        "wrmsr"
        : : "a"(efer_low), "d"(efer_high), "c"(0xC0000080));

    typedef void(__attribute__((sysv_abi)) *kernel_entry_t)(
        const e820_entry_t *, unsigned int);
    kernel_entry_t kernel_entry = (kernel_entry_t)KERNEL_ADDRESS;
    register unsigned long long cr3_register __asm__("rax") = table_address;
    register unsigned long long stack_register __asm__("rbx") = stack_top;
    register const e820_entry_t *map_register __asm__("rdi") = e820_entries;
    register unsigned int count_register __asm__("esi") = e820_count;
    register kernel_entry_t entry_register __asm__("rdx") = kernel_entry;
    __asm__ volatile(
        "movq %%rax, %%cr3\n"
        "movq %%rbx, %%rsp\n"
        "xorq %%rbp, %%rbp\n"
        "jmp *%%rdx\n"
        : "+a"(cr3_register), "+b"(stack_register),
          "+D"(map_register), "+S"(count_register), "+d"(entry_register)
        : : "memory");
    __builtin_unreachable();
}

efi_status_t EFIAPI efi_main(
    efi_handle_t image_handle,
    efi_system_table_t *system_table)
{
    (void)e820_anchor;
    console_write(system_table, "uefi: application entry reached\n");
    *(volatile unsigned char *)0x3F9 = 0;
    *(volatile unsigned char *)0x3FB = 0x80;
    *(volatile unsigned char *)0x3F8 = 1;
    *(volatile unsigned char *)0x3F9 = 0;
    *(volatile unsigned char *)0x3FB = 3;
    *(volatile unsigned char *)0x3FA = 0xC7;
    *(volatile unsigned char *)0x3FC = 0x0B;
    serial_write("uefi: loader started\n");

    efi_allocate_pages_t allocate_pages =
        (efi_allocate_pages_t)boot_service(system_table, 2);
    efi_allocate_pool_t allocate_pool =
        (efi_allocate_pool_t)boot_service(system_table, 5);
    if (!allocate_pages || !allocate_pool
        || load_kernel(
            system_table,
            image_handle,
            allocate_pages,
            allocate_pool) != 0)
    {
        serial_write("uefi: kernel load or handoff failed\n");
        return 0x8000000000000001ULL;
    }

    serial_write("uefi: unexpected return from kernel\n");
    return 0x8000000000000001ULL;
}

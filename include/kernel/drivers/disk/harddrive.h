typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

int ata_read(u32 lba, u32 count, void *dst);
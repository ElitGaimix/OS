#include <kernel/drivers/disk/harddrive.h>

#define ATA_DATA   0x1F0
#define ATA_COUNT  0x1F2
#define ATA_LBA0   0x1F3
#define ATA_LBA1   0x1F4
#define ATA_LBA2   0x1F5
#define ATA_DRIVE  0x1F6
#define ATA_CMD    0x1F7   /* ecriture : commande, lecture : statut */
#define KERNEL_LBA 16
#define KERNEL_ADDRESS 0x100000
#define PML4_ADDRESS 0x70000
#define PDPT_ADDRESS 0x71000
#define PD_ADDRESS 0x72000
#ifndef KERNEL_SECTORS
#define KERNEL_SECTORS 1
#endif
#define USER_ADDRESS 0x400000
#ifndef USER_LBA
#define USER_LBA 2048
#endif
#ifndef USER_SECTORS
#define USER_SECTORS 1
#endif
#define ATA_LBA28_SECTOR_COUNT 0x10000000U
#define ATA_POLL_LIMIT 0x100000U

static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline u16 inw(u16 port) {
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outw(u16 port, u16 v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}

static inline void outb(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static int ata_wait_not_busy(void) {
    for (u32 i = 0; i < ATA_POLL_LIMIT; i++) {
        u8 status = inb(ATA_CMD);
        if (status == 0 || status == 0xFF) return -1;
        if (!(status & 0x80)) return 0;
    }
    return -1;
}

/* Attend que le disque ATA soit pret a transferer des donnees. */
static int ata_wait_drq(void) {
    for (u32 i = 0; i < ATA_POLL_LIMIT; i++) {
        u8 s = inb(ATA_CMD);
        if (s & 0x21) return -1;                   /* ERR ou DF */
        if (s == 0 || s == 0xFF) return -1;
        if (!(s & 0x80) && (s & 0x08)) return 0;   /* BSY=0 et DRQ=1 */
    }
    return -1;
}

/* Lit 'count' secteurs a partir de 'lba' vers 'dst' (n'importe quelle adresse 32 bits) */
/* Utilise le mode ATA PIO pour lire des secteurs LBA28. */
int ata_read(u32 lba, u32 count, void *dst) {
    if ((!dst && count) || lba >= ATA_LBA28_SECTOR_COUNT
        || count > ATA_LBA28_SECTOR_COUNT - lba)
        return -1;

    volatile u16 *p = (volatile u16 *)dst;
    while (count--) {
        if (ata_wait_not_busy() != 0) return -1;
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_COUNT, 1);
        outb(ATA_LBA0, lba & 0xFF);
        outb(ATA_LBA1, (lba >> 8) & 0xFF);
        outb(ATA_LBA2, (lba >> 16) & 0xFF);
        outb(ATA_CMD, 0x20);                       /* READ SECTORS */
        for (int i = 0; i < 4; i++) inb(ATA_CMD);  /* ~400 ns de delai */
        if (ata_wait_drq()) return -1;
        for (int i = 0; i < 256; i++) *p++ = inw(ATA_DATA);
        lba++;
    }
    return 0;
}

int ata_write(u32 lba, u32 count, const void *src) {
    if ((!src && count) || lba >= ATA_LBA28_SECTOR_COUNT
        || count > ATA_LBA28_SECTOR_COUNT - lba)
        return -1;

    const volatile u16 *p = (const volatile u16 *)src;
    while (count--) {
        if (ata_wait_not_busy() != 0) return -1;
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_COUNT, 1);
        outb(ATA_LBA0, lba & 0xFF);
        outb(ATA_LBA1, (lba >> 8) & 0xFF);
        outb(ATA_LBA2, (lba >> 16) & 0xFF);
        outb(ATA_CMD, 0x30);
        for (int i = 0; i < 4; i++) inb(ATA_CMD);
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++) outw(ATA_DATA, *p++);
        if (ata_wait_not_busy() != 0 || (inb(ATA_CMD) & 0x21))
            return -1;
        lba++;
    }

    outb(ATA_CMD, 0xE7);
    if (ata_wait_not_busy() != 0 || (inb(ATA_CMD) & 0x21))
        return -1;
    return 0;
}

/* Identifie le disque primaire maitre et recupere son nombre de secteurs. */
static int ata_identify(u16 info[256], u32 *sector_count) {
    outb(ATA_DRIVE, 0xA0);                       /* disque primaire maitre */
    for (int i = 0; i < 4; i++) inb(ATA_CMD);   /* delai de selection */
    outb(ATA_COUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_CMD, 0xEC);                         /* IDENTIFY DEVICE */

    if (inb(ATA_CMD) == 0 || ata_wait_drq()) return -1;
    for (int i = 0; i < 256; i++) info[i] = inw(ATA_DATA);

    if (!(info[49] & (1 << 9))) return -1;       /* LBA non supporte */
    *sector_count = ((u32)info[61] << 16) | info[60];
    return *sector_count ? 0 : -1;
}
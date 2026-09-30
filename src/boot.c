/* boot.c - execute en mode protege 32 bits, charge en 0x10000 par bootloader.asm
 * Le bootloader 16 bits ne peut pas depasser 1 Mo : ici on lit le disque
 * nous-memes (ATA PIO) pour charger la suite ou on veut, y compris au-dela de 1 Mo. */

#include <stdarg.h>

#define CONSOLE_HISTORY_CAPACITY 2048

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

typedef struct {
    u32 base_low;
    u32 base_high;
    u32 length_low;
    u32 length_high;
    u32 type;
    u32 attributes;
} e820_entry;

typedef struct {
    char character;
    u8 color;
} console_char_t;

typedef struct {
    const char *name;
    const char *permission;

    console_char_t history[CONSOLE_HISTORY_CAPACITY];
    unsigned int history_start;
    unsigned int history_length;
} console_t;

static void history_push(console_t *console, char character, u8 color)
{
    unsigned int index;

    if (console->history_length < CONSOLE_HISTORY_CAPACITY) {
        index = (console->history_start + console->history_length)
                % CONSOLE_HISTORY_CAPACITY;
        console->history_length++;
    } else {
        index = console->history_start;
        console->history_start =
            (console->history_start + 1) % CONSOLE_HISTORY_CAPACITY;
    }

    console->history[index].character = character;
    console->history[index].color = color;
}

console_t history;
int lastrow = 0;

/* Lit un octet depuis un port materiel. */
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
/* Lit un mot de 16 bits depuis un port materiel. */
static inline u16 inw(u16 port) {
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
/* Ecrit un octet dans un port materiel. */
static inline void outb(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void hide_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

/* ---------- affichage VGA texte ---------- */
static volatile u16 *const VGA = (volatile u16 *)0xB8000;

/* Efface l'ecran texte VGA en le remplissant d'espaces. */
static void clear_screen(void) {
    for (int i = 0; i < 80 * 25; i++) VGA[i] = 0x0720;
}

// Don't forget to place a (const char *)0 at the end of the arguments list, otherwise it will crash.
/* Affiche des textes colores sur une ligne VGA, jusqu'au pointeur nul final. */
static void print(int row, const char *text, ...) {
    if (row != lastrow)
        history_push(&history, '\n', 0x00);
    lastrow = row;
    va_list args;
    int column = 0;

    va_start(args, text);
    while (text != (const char *)0) {
        int color = va_arg(args, int);
        for (int i = 0; text[i] && column < 80; i++){
            VGA[row * 80 + column++] = (u16)(((u8)color << 8) | (u8)text[i]);
            history_push(&history, text[i], color);
        }
        text = va_arg(args, const char *);
    }
    va_end(args);
}

//print avec un prefixe specifique a boot.c
#define printfp(row, ...) \
    print((row), "[", 0x0F, __FILE__, 0x0B, "] >>> ", 0x0F, __VA_ARGS__)

/* Affiche un entier non signe en base 10 a la position VGA indiquee. */
static void print_number(int row, int column, u32 value, u8 color) {
    char digits[10];
    int count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    while (count && column < 80) {
        char digit = digits[--count];
        VGA[row * 80 + column++] = (u16)((color << 8) | (u8)digit);
        history_push(&history, (u8)digit, color);
    }
}

/* Calcule la RAM utilisable sous 4 Gio a partir de la carte E820. */
static u32 usable_ram_mib(u32 map_addr, u32 map_count) {
    const e820_entry *entries = (const e820_entry *)map_addr;
    const u64 address_limit = 0x100000000ULL;
    u64 total = 0;

    for (u32 i = 0; i < map_count; i++) {
        if (entries[i].type != 1) continue;

        u64 base = ((u64)entries[i].base_high << 32) | entries[i].base_low;
        u64 length = ((u64)entries[i].length_high << 32) | entries[i].length_low;
        if (base >= address_limit) continue;

        u64 end = base + length;
        if (end < base || end > address_limit) end = address_limit;
        if (end > base) total += end - base;
    }

    return (u32)(total >> 20);
}

/* ---------- ATA PIO (LBA28, disque primaire maitre) ---------- */
#define ATA_DATA   0x1F0
#define ATA_COUNT  0x1F2
#define ATA_LBA0   0x1F3
#define ATA_LBA1   0x1F4
#define ATA_LBA2   0x1F5
#define ATA_DRIVE  0x1F6
#define ATA_CMD    0x1F7   /* ecriture : commande, lecture : statut */
#define ENTRY_LBA 16
#define ENTRY_ADDRESS 0x100000
#ifndef ENTRY_SECTORS
#define ENTRY_SECTORS 1
#endif

/* Attend que le disque ATA soit pret a transferer des donnees. */
static int ata_wait_drq(void) {
    for (u32 i = 0; i < 0x1000000; i++) {
        u8 s = inb(ATA_CMD);
        if (s & 0x21) return -1;                   /* ERR ou DF */
        if (!(s & 0x80) && (s & 0x08)) return 0;   /* BSY=0 et DRQ=1 */
    }
    return -1;
}

/* Lit 'count' secteurs a partir de 'lba' vers 'dst' (n'importe quelle adresse 32 bits) */
/* Utilise le mode ATA PIO pour lire des secteurs LBA28. */
static int ata_read(u32 lba, u32 count, void *dst) {
    volatile u16 *p = (volatile u16 *)dst;
    while (count--) {
        while (inb(ATA_CMD) & 0x80) {}             /* attendre BSY=0 */
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

/* ---------- point d'entree (place en tete du binaire par linker.ld) ---------- */
/* Initialise l'ecran, affiche les informations systeme et lance le module out. */
__attribute__((section(".text.boot")))
void boot_main(u32 map_addr, u32 map_count) {
    hide_cursor();
    clear_screen();
    printfp(0,"Boot 16 bits -> mode protege 32 bits : ",0X0F, "OK", 0x0A, (const char *)0);
    printfp(1, "En cours d'execution", 0x0F, (const char *)0);

    if (map_count) {
        printfp(2, "RAM utilisable (<4 Gio), Mio: ", 0x0F, (const char *)0);
        print_number(2, 43, usable_ram_mib(map_addr, map_count), 0x0A);
    } else {
        printfp(2, "Carte memoire E820: ECHEC", 0x0C, (const char *)0);
    }

    u16 identify_info[256];
    u32 sector_count;
    if (ata_identify(identify_info, &sector_count) == 0) {
        printfp(3, "Capacite disque LBA28, Mio: ", 0x0F, (const char *)0);
        print_number(3, 41, sector_count >> 11, 0x0A);
    } else {
        printfp(3, "ATA IDENTIFY: ECHEC", 0x0C, (const char *)0);
    }

    /* Test : relire le secteur de boot (LBA 0) a 0x100000, donc au-dela de 1 Mo */
    volatile u8 *high = (volatile u8 *)0x100000;
    if (ata_read(0, 1, (void *)high) == 0 && high[510] == 0x55 && high[511] == 0xAA)
        printfp(4, "Lecture disque a 1 Mo (ATA PIO) : ",0X0F, "OK", 0x0A, (const char *)0);
    else
        printfp(4, "Lecture disque a 1 Mo (ATA PIO) : ",0X0F, "ECHEC", 0x0C, (const char *)0);

    if (ata_read(ENTRY_LBA, ENTRY_SECTORS, (void *)ENTRY_ADDRESS) == 0){
        history_push(&history, '\n', 0x00);
        ((void (*)(console_t *))ENTRY_ADDRESS)(&history);
    } else
        printfp(5, "Echec du chargement de entry.bin", 0x0C, (const char *)0);
}

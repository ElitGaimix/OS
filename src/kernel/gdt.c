#include <kernel/gdt.h>

typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

struct tss {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iopb;
} __attribute__((packed));

struct gdtr {
    u16 limit;
    u64 base;
} __attribute__((packed));

static u64 gdt[9];
static struct tss tss;
static unsigned char kernel_stack[16384] __attribute__((aligned(16)));
static struct gdtr gdtr;

void gdt_init(void)
{
    for (unsigned i = 0; i < sizeof(tss); i++)
        ((unsigned char *)&tss)[i] = 0;          // pas de .bss garanti à zéro

    gdt[0] = 0;
    gdt[1] = 0;
    gdt[2] = 0;
    gdt[3] = 0x00AF9A000000FFFFULL;              // code noyau 64 bits
    gdt[4] = 0x00CF92000000FFFFULL;              // data noyau
    gdt[5] = 0x00CFF2000000FFFFULL;              // data user (DPL 3)
    gdt[6] = 0x00AFFA000000FFFFULL;              // code user 64 bits (DPL 3)

    u64 base = (u64)&tss;
    u32 limit = sizeof(tss) - 1;
    gdt[7] = (limit & 0xFFFF)
           | ((base & 0xFFFF) << 16)
           | (((base >> 16) & 0xFF) << 32)
           | (0x89ULL << 40)                     // TSS 64 bits disponible
           | ((u64)((limit >> 16) & 0xF) << 48)
           | (((base >> 24) & 0xFF) << 56);
    gdt[8] = base >> 32;

    tss.rsp0 = (u64)kernel_stack + sizeof(kernel_stack);
    tss.iopb = sizeof(tss);

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u64)gdt;

    __asm__ volatile(
        "lgdt %0\n"
        "pushq $0x18\n"                          // recharge CS avec un far return
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x20, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw $0x38, %%ax\n"
        "ltr %%ax\n"
        : : "m"(gdtr) : "rax", "memory");
}

void gdt_set_kernel_stack(u64 stack_top)
{
	tss.rsp0 = stack_top;
}
#include <kernel/interrupts.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define IDT_ENTRIES 256

typedef struct {
	u16 offset_low;
	u16 selector;
	u8 ist;
	u8 attributes;
	u16 offset_middle;
	u32 offset_high;
	u32 reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
	u16 limit;
	u64 base;
} __attribute__((packed)) idtr_t;

static idt_entry_t idt[IDT_ENTRIES];

void interrupts_init(void)
{
	idtr_t idtr;
	u64 *idt_words = (u64 *)idt;

	__asm__ volatile("cli" ::: "memory");
	for (u32 i = 0; i < IDT_ENTRIES * 2; i++)
		idt_words[i] = 0;

	idtr.limit = sizeof(idt) - 1;
	idtr.base = (u64)(unsigned long long)idt;
	__asm__ volatile("lidt %0" : : "m"(idtr));
}

void interrupt_register_handler(u8 vector, u64 address)
{
	idt[vector].offset_low = (u16)address;
	idt[vector].selector = 0x18;
	idt[vector].ist = 0;
	idt[vector].attributes = 0x8E;
	idt[vector].offset_middle = (u16)(address >> 16);
	idt[vector].offset_high = (u32)(address >> 32);
	idt[vector].reserved = 0;
}
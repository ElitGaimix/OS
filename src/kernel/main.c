
#include <kernel/main.h>
#include <kernel/panic.h>
#include <kernel/kshell.h>
#include <kernel/drivers/inputs/keyboard.h>
#include <kernel/process/pit.h>
#include <kernel/gdt.h>
#include <kernel/serial.h>
#include <kernel/memory/heap.h>
#include <kernel/net/net.h>

typedef unsigned char u8;

extern u8 __bss_start[];
extern u8 __bss_end[];

__attribute__((section(".text.boot"))) void kmain(
    const e820_entry_t *memory_map, e820_u32_t entry_count)
{
	volatile u8 *bss = __bss_start;
	while (bss < __bss_end)
		*bss++ = 0;

	kernel_heap_init();
	if (serial_init() == 0)
		serial_write("kernel: serial ready\n");

	panic_init();
	gdt_init();
	serial_write("kernel: initializing services\n");
	kshell_init(memory_map, entry_count);
	net_init();
	pit_init();
	print_prefix();
	println("Main event loop started.", 0x0A);
	serial_write("kernel: boot complete\n");
	for (;;) {
		net_poll();
		keyboard_poll();
		keyboard_wait_for_interrupt();
	}
}
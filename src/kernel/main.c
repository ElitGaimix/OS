
#include <kernel/main.h>
#include <kernel/panic.h>
#include <kernel/kshell.h>
#include <kernel/drivers/inputs/keyboard.h>
#include <kernel/process/pit.h>
#include <kernel/gdt.h>

__attribute__((section(".text.boot"))) void kmain(
    const e820_entry_t *memory_map, e820_u32_t entry_count)
{
	panic_init();
	gdt_init();
	kshell_init(memory_map, entry_count);
	pit_init();
	print_prefix();
	println("Main event loop started.", 0x0A);
	for (;;) {
		keyboard_poll();
		keyboard_wait_for_interrupt();
	}
}
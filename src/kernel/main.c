
#include "shell.h"

static console_t kernel_console;

void print_prefix(){
	print("[", 0x0F);
	print("KERNEL",0x03);
	print("] ", 0x0F);
}

__attribute__((section(".text.boot"))) void kmain(void)
{
	unsigned int bit_count = sizeof(void *) * 8;
	char digits[10];
	unsigned int digit_count = 0;

	do {
		digits[digit_count++] = (char)('0' + bit_count % 10);
		bit_count /= 10;
	} while (bit_count);

	char bit_count_text[11];
	for (unsigned int i = 0; i < digit_count; i++)
		bit_count_text[i] = digits[digit_count - i - 1];
	bit_count_text[digit_count] = '\0';

	kernel_console.history_start = 0;
	kernel_console.history_length = 0;
	init(&kernel_console);
	// test();
	print_prefix();
	print("Starting kernel in ", 0x0F);
	print(bit_count_text, 0x0A);
	println(" bits...", 0x0F);
	draw_input_line("");

	kernel_keyboard_event_t event;
	for (;;)
		keyboard_wait_event(&event);
}
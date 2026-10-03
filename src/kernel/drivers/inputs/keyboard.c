#include <kernel/drivers/inputs/keyboard.h>
#include <kernel/interrupts.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define KEYBOARD_VECTOR 0x21
#define KEYBOARD_QUEUE_SIZE 64
#define KEYBOARD_QUEUE_MASK (KEYBOARD_QUEUE_SIZE - 1)
#define KEYBOARD_CALLBACK_CAPACITY 16

static volatile u8 scancode_queue[KEYBOARD_QUEUE_SIZE];
static volatile u32 queue_head;
static volatile u32 queue_tail;
static keyboard_event_callback_t callbacks[KEYBOARD_CALLBACK_CAPACITY];
static u32 callback_count;
static int shift_down;
static int caps_lock;
static int extended_scancode;

static inline __attribute__((no_caller_saved_registers)) u8 inb(u16 port)
{
	u8 value;
	__asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static inline __attribute__((no_caller_saved_registers)) void outb(u16 port, u8 value)
{
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void keyboard_irq_handler(interrupt_frame_t *frame)
	__attribute__((interrupt));

static void keyboard_irq_handler(interrupt_frame_t *frame)
{
	(void)frame;

	u8 scancode = inb(0x60);
	u32 next_head = (queue_head + 1) & KEYBOARD_QUEUE_MASK;
	if (next_head != queue_tail) {
		scancode_queue[queue_head] = scancode;
		queue_head = next_head;
	}

	outb(0x20, 0x20);
}

static void pic_remap(void)
{
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);

	outb(0x20, 0x11);
	outb(0xA0, 0x11);
	outb(0x21, 0x20);
	outb(0xA1, 0x28);
	outb(0x21, 0x04);
	outb(0xA1, 0x02);
	outb(0x21, 0x01);
	outb(0xA1, 0x01);

	outb(0x21, 0xFD);
	outb(0xA1, 0xFF);
}

void keyboard_init(void)
{
	__asm__ volatile("cli" ::: "memory");

	queue_head = 0;
	queue_tail = 0;
	shift_down = 0;
	caps_lock = 0;
	extended_scancode = 0;

	interrupt_register_handler(
		KEYBOARD_VECTOR,
		(u64)(unsigned long long)keyboard_irq_handler);

	pic_remap();
	__asm__ volatile("sti" ::: "memory");
}

int keyboard_register_callback(keyboard_event_callback_t callback)
{
	if (!callback || callback_count == KEYBOARD_CALLBACK_CAPACITY)
		return 0;

	callbacks[callback_count++] = callback;
	return 1;
}

static void dispatch_callbacks(const kernel_keyboard_event_t *event)
{
	for (u32 i = 0; i < callback_count; i++)
		callbacks[i](event);
}

static int translate_scancode(u8 scancode, kernel_keyboard_event_t *event)
{
	static const char keymap_normal[128] = {
		[0x02] = '&', [0x03] = (char)0x82, [0x04] = '"', [0x05] = '\'',
		[0x06] = '(', [0x07] = '-', [0x08] = (char)0x8A, [0x09] = '_',
		[0x0A] = (char)0x87, [0x0B] = (char)0x85, [0x0C] = ')', [0x0D] = '=',
		[0x10] = 'a', [0x11] = 'z', [0x12] = 'e', [0x13] = 'r',
		[0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
		[0x18] = 'o', [0x19] = 'p', [0x1A] = '^', [0x1B] = '$',
		[0x1E] = 'q', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
		[0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
		[0x26] = 'l', [0x27] = 'm', [0x28] = (char)0x97, [0x29] = (char)0xFD,
		[0x2B] = '*', [0x2C] = 'w', [0x2D] = 'x', [0x2E] = 'c',
		[0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = ',',
		[0x33] = ';', [0x34] = ':', [0x35] = '!', [0x39] = ' '
	};
	static const char keymap_shift[128] = {
		[0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
		[0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
		[0x0A] = '9', [0x0B] = '0', [0x0C] = (char)0xF8, [0x0D] = '+',
		[0x10] = 'A', [0x11] = 'Z', [0x12] = 'E', [0x13] = 'R',
		[0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
		[0x18] = 'O', [0x19] = 'P', [0x1A] = '^', [0x1B] = (char)0x9C,
		[0x1E] = 'Q', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
		[0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
		[0x26] = 'L', [0x27] = 'M', [0x28] = '%', [0x29] = (char)0xFD,
		[0x2B] = (char)0xE6, [0x2C] = 'W', [0x2D] = 'X', [0x2E] = 'C',
		[0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = '?',
		[0x33] = '.', [0x34] = '/', [0x35] = (char)0xF5, [0x39] = ' '
	};
	u8 key = scancode & 0x7F;
	if (scancode == 0xE0) {
		extended_scancode = 1;
		return 0;
	}
	if (extended_scancode) {
		extended_scancode = 0;
		if (scancode & 0x80)
			return 0;
		if (key == 0x48) {
			event->type = KERNEL_KEY_EVENT_HISTORY_UP;
			event->character = 0;
			return 1;
		}
		if (key == 0x50) {
			event->type = KERNEL_KEY_EVENT_HISTORY_DOWN;
			event->character = 0;
			return 1;
		}
		return 0;
	}
	if (key == 0x2A || key == 0x36) {
		shift_down = !(scancode & 0x80);
		return 0;
	}
	if (scancode & 0x80)
		return 0;

	if (key == 0x3A) {
		caps_lock = !caps_lock;
		return 0;
	}
	if (key == 0x1C) {
		event->type = KERNEL_KEY_EVENT_ENTER;
		event->character = '\n';
		return 1;
	}
	if (key == 0x0E) {
		event->type = KERNEL_KEY_EVENT_BACKSPACE;
		event->character = '\b';
		return 1;
	}

	char character = keymap_normal[key];
	if (!character)
		return 0;

	int is_letter = character >= 'a' && character <= 'z';
	if ((is_letter && (shift_down != caps_lock)) || (!is_letter && shift_down))
		character = keymap_shift[key];

	event->type = KERNEL_KEY_EVENT_CHARACTER;
	event->character = character;
	return 1;
}

static int dequeue_scancode(u8 *scancode)
{
	__asm__ volatile("cli" ::: "memory");
	if (queue_tail == queue_head) {
		__asm__ volatile("sti" ::: "memory");
		return 0;
	}

	*scancode = scancode_queue[queue_tail];
	queue_tail = (queue_tail + 1) & KEYBOARD_QUEUE_MASK;
	__asm__ volatile("sti" ::: "memory");
	return 1;
}

void keyboard_poll(void)
{
	u8 scancode;
	kernel_keyboard_event_t event;
	if (dequeue_scancode(&scancode)
		&& translate_scancode(scancode, &event))
		dispatch_callbacks(&event);
}

void keyboard_wait_for_interrupt(void)
{
	__asm__ volatile("cli" ::: "memory");
	if (queue_tail == queue_head)
		__asm__ volatile("sti; hlt" ::: "memory");
	else
		__asm__ volatile("sti" ::: "memory");
}
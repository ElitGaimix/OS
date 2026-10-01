#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

typedef enum {
	KERNEL_KEY_EVENT_CHARACTER,
	KERNEL_KEY_EVENT_ENTER,
	KERNEL_KEY_EVENT_BACKSPACE,
	KERNEL_KEY_EVENT_HISTORY_UP,
	KERNEL_KEY_EVENT_HISTORY_DOWN
} kernel_keyboard_event_type_t;

typedef struct {
	kernel_keyboard_event_type_t type;
	char character;
} kernel_keyboard_event_t;

typedef void (*keyboard_event_callback_t)(const kernel_keyboard_event_t *event);

void keyboard_init(void);
int keyboard_register_callback(keyboard_event_callback_t callback);
void keyboard_poll(void);
int keyboard_wait_event(kernel_keyboard_event_t *event);

#endif
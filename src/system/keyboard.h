#ifndef SYSTEM_KEYBOARD_H
#define SYSTEM_KEYBOARD_H

typedef enum {
    KEYBOARD_EVENT_CHARACTER,
    KEYBOARD_EVENT_ENTER,
    KEYBOARD_EVENT_BACKSPACE
} keyboard_event_type_t;

typedef struct {
    keyboard_event_type_t type;
    char character;
} keyboard_event_t;

void keyboard_init(void);
int keyboard_wait_event(keyboard_event_t *event);

#endif
#include "entry.h"
#include "keyboard.h"

static char *int_to_char(unsigned int value, char buffer[11])
{
    char digits[10];
    unsigned int count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    for (unsigned int i = 0; i < count; i++)
        buffer[i] = digits[count - i - 1];
    buffer[count] = '\0';
    return buffer;
}
/* Affiche un message de test sur la cinquieme ligne de l'ecran VGA. */
void entry(console_t *main_console) {
    init(main_console);
    test();
    for(int i = 0; i <= 20; i++){
        char number[11];
        print("[", 0x0F);
        print(int_to_char((unsigned int)i, number), 0x0A);
        print("] ", 0x0F);
        println("Test console", 0x0B);
    }

    char input[79];
    unsigned int input_length = 0;
    keyboard_event_t event;

    input[0] = '\0';
    draw_input_line(input);
    keyboard_init();

    for (;;) {
        if (!keyboard_wait_event(&event))
            continue;

        if (event.type == KEYBOARD_EVENT_CHARACTER) {
            if (input_length < sizeof(input) - 1) {
                input[input_length++] = event.character;
                input[input_length] = '\0';
                draw_input_line(input);
            }
        } else if (event.type == KEYBOARD_EVENT_BACKSPACE) {
            if (input_length) {
                input[--input_length] = '\0';
                draw_input_line(input);
            }
        } else if (event.type == KEYBOARD_EVENT_ENTER) {
            print("> ", 0x0A);
            print(input, 0x0F);
            println("", 0x0F);
            input_length = 0;
            input[0] = '\0';
            draw_input_line(input);
        }
    }
}
#include "out.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

console_t *main_console;
static volatile u16 *const VGA = (volatile u16 *)0xB8000;

static inline void outb(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

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

void init(console_t *console)
{
    main_console = console;
    reaload();
}

console_t *get_console(void)
{
    return main_console;
}

void clear_screen(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA[i] = 0x0720;
}

static void scroll_screen(void)
{
    for (int row = 1; row < VGA_HEIGHT - 1; row++) {
        for (int column = 0; column < VGA_WIDTH; column++) {
            VGA[(row - 1) * VGA_WIDTH + column] =
                VGA[row * VGA_WIDTH + column];
        }
    }

    for (int column = 0; column < VGA_WIDTH; column++)
        VGA[(VGA_HEIGHT - 2) * VGA_WIDTH + column] = 0x0720;
}

static void next_line(int *row, int *column)
{
    *column = 0;
    (*row)++;
    if (*row >= VGA_HEIGHT - 1) {
        scroll_screen();
        *row = VGA_HEIGHT - 2;
    }
}

void reaload(void)
{
    int row = 0;
    int column = 0;

    clear_screen();
    for (unsigned int i = 0; i < main_console->history_length; i++) {
        unsigned int index = (main_console->history_start + i)
                             % CONSOLE_HISTORY_CAPACITY;
        console_char_t character = main_console->history[index];

        if (character.character == '\n') {
            next_line(&row, &column);
            continue;
        }

        if (column >= VGA_WIDTH){
            next_line(&row, &column);
            VGA[row * VGA_WIDTH + column++] =
            (u16)((0x0F << 8) | '-');
            VGA[row * VGA_WIDTH + column++] =
            (u16)((0x0F << 8) | ' ');
        }

        VGA[row * VGA_WIDTH + column++] =
            (u16)(((u16)character.color << 8) | (u8)character.character);
    }
    set_cursor(row,column);
}

void print(const char *text, const u8 color)
{
    for (int i = 0; text[i]; i++)
        history_push(main_console, text[i], color);
    reaload();
}

void println(const char *text, const u8 color)
{
    for (int i = 0; text[i]; i++)
        history_push(main_console, text[i], color);
    history_push(main_console, '\n', color);
    reaload();
}

void draw_input_line(const char *text)
{
    unsigned int column = 0;
    unsigned int index = (VGA_HEIGHT - 1) * VGA_WIDTH;

    for (unsigned int i = 0; i < VGA_WIDTH; i++)
        VGA[index + i] = 0x0720;

    VGA[index + column++] = (u16)((0x0A << 8) | '>');
    VGA[index + column++] = (u16)((0x0A << 8) | ' ');

    while (*text && column < VGA_WIDTH) {
        VGA[index + column++] = (u16)((0x0F << 8) | (u8)*text++);
    }

    set_cursor(VGA_HEIGHT - 1, (int)column);
}

void set_cursor(int row, int column)
{
    u16 position = (u16)(row * 80 + column);

    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0E);  // réactive le curseur, masqué au démarrage
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)position);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)(position >> 8));
}

void test()
{
    println("Test out.c", 0x0F);
    print("Result : ", 0x0F);
    println("OK", 0x0A);
}
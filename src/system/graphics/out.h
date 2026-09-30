#ifndef SYSTEM_GRAPHICS_OUT_H
#define SYSTEM_GRAPHICS_OUT_H

#define CONSOLE_HISTORY_CAPACITY 2048


typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;


typedef struct {
    char character;
    u8 color;
} console_char_t;

typedef struct {
    const char *name;
    const char *permission;

    console_char_t history[CONSOLE_HISTORY_CAPACITY];
    unsigned int history_start;
    unsigned int history_length;
} console_t;

extern console_t *main_console;

void init(console_t *console);

console_t *get_console(void);

void clear_screen(void);

void reaload(void);

void print(const char *text, const u8 color);

void println(const char *text, const u8 color);

void draw_input_line(const char *text);

void test();

void set_cursor(int row, int column);

#endif
#ifndef KERNEL_SHELL_H
#define KERNEL_SHELL_H

#include "keyboard/keyboard.h"

#define CONSOLE_HISTORY_CAPACITY 2048

typedef struct {
	char character;
	unsigned char color;
} console_char_t;

typedef struct {
	console_char_t history[CONSOLE_HISTORY_CAPACITY];
	unsigned int history_start;
	unsigned int history_length;
} console_t;

void init(console_t *console);
console_t *get_console(void);
void clear_screen(void);
void reaload(void);
void print(const char *text, unsigned char color);
void println(const char *text, unsigned char color);
void draw_input_line(const char *text);
void set_cursor(int row, int column);
void test(void);

#endif
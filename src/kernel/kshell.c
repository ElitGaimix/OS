
#include <e820.h>
#include <kernel/drivers/inputs/keyboard.h>
#include <kernel/kshell.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define CONSOLE_HISTORY_CAPACITY 2048

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long long u64;

typedef struct {
    char character;
    unsigned char color;
} console_char_t;

typedef struct {
    console_char_t history[CONSOLE_HISTORY_CAPACITY];
    unsigned int history_start;
    unsigned int history_length;
} console_t;

static console_t kernel_console;
static volatile u16 *const VGA = (volatile u16 *)0xB8000;

static inline void outb(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void history_push(char character, u8 color)
{
    unsigned int index;

    if (kernel_console.history_length < CONSOLE_HISTORY_CAPACITY) {
        index = (kernel_console.history_start + kernel_console.history_length)
                % CONSOLE_HISTORY_CAPACITY;
        kernel_console.history_length++;
    } else {
        index = kernel_console.history_start;
        kernel_console.history_start =
            (kernel_console.history_start + 1) % CONSOLE_HISTORY_CAPACITY;
    }

    kernel_console.history[index].character = character;
    kernel_console.history[index].color = color;
}

static void set_cursor(int row, int column)
{
    u16 position = (u16)(row * 80 + column);

    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0E);  // réactive le curseur, masqué au démarrage
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)position);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)(position >> 8));
}

static void clear_screen(void)
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

static void reaload(void)
{
    int row = 0;
    int column = 0;

    clear_screen();
    for (unsigned int i = 0; i < kernel_console.history_length; i++) {
        unsigned int index = (kernel_console.history_start + i)
                             % CONSOLE_HISTORY_CAPACITY;
        console_char_t character = kernel_console.history[index];

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

void print(const char *text, u8 color)
{
    for (int i = 0; text[i]; i++)
        history_push(text[i], color);
    reaload();
}

void println(const char *text, u8 color)
{
    for (int i = 0; text[i]; i++)
        history_push(text[i], color);
    history_push('\n', color);
    reaload();
}

static e820_u64_t usable_ram_mib(
    const e820_entry_t *entries, e820_u32_t entry_count)
{
    e820_u64_t total = 0;
    const e820_u64_t max_value = ~(e820_u64_t)0;

    for (e820_u32_t i = 0; i < entry_count; i++) {
        if (entries[i].type != 1) continue;

        e820_u64_t length =
            ((e820_u64_t)entries[i].length_high << 32) | entries[i].length_low;
        if (length > max_value - total) return max_value >> 20;
        total += length;
    }

    return total >> 20;
}

static void print_u64(e820_u64_t value)
{
    char digits[21];
    unsigned int position = sizeof(digits) - 1;
    digits[position] = '\0';

    do {
        digits[--position] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    print(&digits[position], 0x0A);
}

static void draw_input_line(const char *prefix,const u8 color, const char *text)
{
    unsigned int column = 0;
    unsigned int index = (VGA_HEIGHT - 1) * VGA_WIDTH;

    for (unsigned int i = 0; i < VGA_WIDTH; i++)
        VGA[index + i] = 0x0720;

    while (*prefix && column < VGA_WIDTH) {
        VGA[index + column++] = (u16)((color << 8) | (u8)*prefix++);
    }
    VGA[index + column++] = (u16)((0x0A << 8) | ' ');
    VGA[index + column++] = (u16)((0x0A << 8) | '>');
    VGA[index + column++] = (u16)((0x0A << 8) | ' ');

    while (*text && column < VGA_WIDTH) {
        VGA[index + column++] = (u16)((0x0F << 8) | (u8)*text++);
    }

    set_cursor(VGA_HEIGHT - 1, (int)column);
}

void test(void)
{
    println("Test out.c", 0x0F);
    print("Result : ", 0x0F);
    println("OK", 0x0A);
}
 
static char input[79];
static unsigned int input_length = 0;

void shutdown(void) {
    __asm__ volatile ("outw %0, %w1" : : "a"((u16)0x2000), "Nd"(0x604));  // QEMU récent (machine q35 et i440fx récents)
    __asm__ volatile ("outw %0, %w1" : : "a"((u16)0x2000), "Nd"(0xB004));  // anciens QEMU / Bochs
    __asm__ volatile ("outw %0, %w1" : : "a"((u16)0x3400), "Nd"(0x4004));  // VirtualBox
    for (;;) __asm__ volatile ("hlt");  // au cas où rien n'a marché
}

static int input_matches(const char *command)
{
    unsigned int index = 0;
    while (input[index] && input[index] == command[index]) {
        index++;
    }
    return input[index] == '\0' && command[index] == '\0';
}

static int input_starts_with_command(const char *command)
{
    unsigned int index = 0;
    while (command[index] && input[index] == command[index])
        index++;

    return command[index] == '\0'
        && (input[index] == '\0' || input[index] == ' ');
}

static void execute_panic_command(void)
{
    if (input_matches("panic") || input_matches("panic help")) {
        println("panic: div0 | ud | bp | page | gpf", 0x0F);
    } else if (input_matches("panic div0")) {
        volatile int zero = 0;
        volatile int result = 1 / zero;
        (void)result;  // pour éviter un avertissement de compilation
    } else if (input_matches("panic ud")) {
        __asm__ volatile("ud2");
    } else if (input_matches("panic bp")) {
        __asm__ volatile("int3");
    } else if (input_matches("panic page")) {
        *(volatile unsigned long long *)0xFFFF800000000000ULL = 0;
    } else if (input_matches("panic gpf")) {
        __asm__ volatile(
            "movw $0xFFFF, %%ax\n\t"
            "movw %%ax, %%ds"
            : : : "rax");
    } else {
        println("panic: div0 | ud | bp | page | gpf", 0x0F);
    }
}

void enter_user(u64 rip, u64 rsp)
{
    __asm__ volatile(
        "pushq $0x2B\n"        // SS user
        "pushq %0\n"           // RSP user
        "pushq $0x202\n"       // RFLAGS : IF=1
        "pushq $0x33\n"        // CS user
        "pushq %1\n"           // RIP
        "iretq\n"
        : : "r"(rsp), "r"(rip) : "memory");
    __builtin_unreachable();
}

void run_user_program(void)
{
    enter_user(0x400000, 0x5FFFF8);
}

static void keyboad_callback(const kernel_keyboard_event_t *event)
{
    if (event->type == KERNEL_KEY_EVENT_CHARACTER) {
            if (input_length < sizeof(input) - 1) {
                input[input_length++] = event->character;
                input[input_length] = '\0';
                draw_input_line("kernel", 0x0B, input);
            }
        } else if (event->type == KERNEL_KEY_EVENT_BACKSPACE) {
            if (input_length) {
                input[--input_length] = '\0';
                draw_input_line("kernel", 0x0B, input);
            }
        } else if (event->type == KERNEL_KEY_EVENT_ENTER) {
            // TODO: Execute command
            print("kernel", 0x0B);
            print(" > ", 0x0A);
            print(input, 0x0F);
            println("", 0x0F);
            if (input_starts_with_command("panic")) {
                execute_panic_command();
            }
            if (input_matches("shutdown")) {
                shutdown();
            }
            if (input_matches("test")) {
                test();
            }
            if (input_matches("usertest")) {
                run_user_program();
            }
            input_length = 0;
            input[0] = '\0';
            draw_input_line("kernel", 0x0B, input);
        }
}

void print_prefix(void)
{
    print("[", 0x0F);
    print("KERNEL", 0x03);
    print("] ", 0x0F);
}

void kshell_init(const e820_entry_t *memory_map, e820_u32_t entry_count)
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
    input[0] = '\0';
    keyboard_register_callback(keyboad_callback);
    keyboard_init();
    reaload();
    print_prefix();
    print("Starting kernel in ", 0x0F);
    print(bit_count_text, 0x0A);
    println(" bits...", 0x0F);
    print_prefix();
    print("RAM utilisable: ", 0x0F);
    print_u64(usable_ram_mib(memory_map, entry_count));
    println(" Mio", 0x0F);
    draw_input_line("kernel", 0x0B, "");
}
/*  KengaOS — PS/2 keyboard driver (i8042) + RU/US layouts.
    Использует IRQ1 для асинхронного приёма сканкодов.
    Буфер — кольцевой, на 64 символа.

    Раскладки:
      0 = US (QWERTY) — латиница
      1 = RU (ЙЦУКЕН) — кириллица

    Переключение — через kbd_set_layout(). Когда включена RU, символы
    кириллицы добавляются в буфер как UTF-8 последовательности (2 байта
    на символ).
*/
#include "kbd.h"
#include "uart.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/idt.h"
#include "pit.h"

#define KBD_DATA  0x60
#define KBD_CMD   0x64
#define KBD_STAT  0x64

#define KBD_STAT_OUTBUF 0x01

/* US QWERTY scancode set 1 → ASCII (только печатные + управляющие) */
static const char scancode_us[128] = {
    0, 0x1B, '1','2','3','4','5','6','7','8','9','0','-','=', 0x08,
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0, '*',
    0, ' ', 0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '-','5','6','+', 0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* RU ЙЦУКЕН: для каждой клавиши US — соответствующий русский символ.
   Кириллица U+0430..U+044F (а..я) и U+0410..U+042F (А..Я).
   Возвращается как codepoint (u32), конвертируется в UTF-8 в callback. */
static const u32 scancode_ru[128] = {
    0, 0x1B, '1','2','3','4','5','6','7','8','9','0','-','=', 0x08,
    '\t',
    0x439, /* й */
    0x446, /* ц */
    0x443, /* у */
    0x43A, /* к */
    0x435, /* е */
    0x43D, /* н */
    0x433, /* г */
    0x448, /* ш */
    0x449, /* щ */
    0x437, /* з */
    0x445, /* х */
    0x44A, /* ъ */
    '\n',
    0,
    0x444, /* ф */
    0x44B, /* ы */
    0x432, /* в */
    0x430, /* а */
    0x43F, /* п */
    0x440, /* р */
    0x43E, /* о */
    0x43B, /* л */
    0x434, /* д */
    0x436, /* ж */
    0x44D, /* э */
    '\\',
    0,
    '\\',
    0x44F, /* я */
    0x447, /* ч */
    0x441, /* с */
    0x43C, /* м */
    0x438, /* и */
    0x442, /* т */
    0x44C, /* ь */
    0x431, /* б */
    0x44E, /* ю */
    '.',  /* , в RU, но упростим до '.' */
    '.',  /* . в RU */
    '/',
    0, '*',
    0, ' ', 0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '-','5','6','+', 0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* Буквы RU, которые должны становиться заглавными при Shift.
   Также Ё/ё (но они редки в раскладке ЙЦУКЕН). */
static u32 ru_to_upper(u32 cp) {
    if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;   /* а..я → А..Я */
    if (cp == 0x451) return 0x401;   /* ё → Ё */
    return cp;
}

#define KBD_BUF_SIZE 256
static u8 kbd_buf[KBD_BUF_SIZE];
static volatile u32 kbd_buf_head = 0;
static volatile u32 kbd_buf_tail = 0;

static bool shift_pressed = false;
static bool caps_lock = false;
static int current_layout = 0;   /* 0 = US, 1 = RU */

void kbd_set_layout(int layout) {
    if (layout == 0 || layout == 1) current_layout = layout;
}

int kbd_get_layout(void) {
    return current_layout;
}

void kbd_toggle_layout(void) {
    current_layout = !current_layout;
}

static char shift_translate_us(char c) {
    if (c >= 'a' && c <= 'z') {
        if (caps_lock) return c;
        return c - 32;
    }
    if (c >= 'A' && c <= 'Z') {
        if (caps_lock) return c + 32;
        return c;
    }
    switch (c) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case ';': return ':';
        case '\'': return '"';
        case '`': return '~';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
    }
    return c;
}

static void kbd_push_byte(u8 b) {
    u32 next = (kbd_buf_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_buf_tail) return;   /* буфер полон */
    kbd_buf[kbd_buf_head] = b;
    kbd_buf_head = next;
}

/* Кодировать codepoint в UTF-8 и положить в буфер. */
static void kbd_push_utf8(u32 cp) {
    if (cp < 0x80) {
        kbd_push_byte((u8)cp);
    } else if (cp < 0x800) {
        kbd_push_byte((u8)(0xC0 | (cp >> 6)));
        kbd_push_byte((u8)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        kbd_push_byte((u8)(0xE0 | (cp >> 12)));
        kbd_push_byte((u8)(0x80 | ((cp >> 6) & 0x3F)));
        kbd_push_byte((u8)(0x80 | (cp & 0x3F)));
    }
}

static void kbd_callback(void *ctx) {
    (void)ctx;
    if (!(inb(KBD_STAT) & KBD_STAT_OUTBUF)) return;
    u8 sc = inb(KBD_DATA);

    if (sc == 0x2A || sc == 0x36) { shift_pressed = true; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_pressed = false; return; }
    if (sc == 0x3A) { caps_lock = !caps_lock; return; }
    /* F12 — toggle layout */
    if (sc == 0x58) { kbd_toggle_layout(); return; }

    if (sc & 0x80) return;   /* key release */
    if (sc >= 128) return;

    if (current_layout == 1) {
        /* RU layout */
        u32 cp = scancode_ru[sc];
        if (cp == 0) return;
        if (cp < 0x80) {
            /* ASCII character (digits, punct) — shift работает как в US */
            if (shift_pressed) cp = shift_translate_us((char)cp);
            kbd_push_byte((u8)cp);
        } else {
            /* Cyrillic letter — CapsLock инвертирует, Shift делает заглавной */
            if (caps_lock ^ shift_pressed) {
                cp = ru_to_upper(cp);
            }
            kbd_push_utf8(cp);
        }
    } else {
        /* US layout */
        char c = scancode_us[sc];
        if (c == 0) return;
        if (shift_pressed) c = shift_translate_us(c);
        kbd_push_byte((u8)c);
    }
}

void kbd_init(void) {
    irq_register(1, kbd_callback);
    while (inb(KBD_STAT) & KBD_STAT_OUTBUF) inb(KBD_DATA);
    kbd_buf_head = kbd_buf_tail = 0;
    current_layout = 0;
}

bool kbd_has_char(void) {
    return kbd_buf_head != kbd_buf_tail;
}

char kbd_getc(void) {
    while (!kbd_has_char()) {
        __asm__ volatile ("sti; hlt");
    }
    char c = kbd_buf[kbd_buf_tail];
    kbd_buf_tail = (kbd_buf_tail + 1) % KBD_BUF_SIZE;
    return c;
}

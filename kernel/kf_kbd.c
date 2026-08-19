/* kf_kbd.c — PS/2 keyboard driver (M2.3).
 *
 * IRQ1 (keyboard) is remapped to vector 33. The common ISR (intr.c) calls
 * k_kbd_irq() on vector 33; we read the scancode from port 0x60, translate
 * set-1 make codes to ASCII (US layout) and push into a ring buffer.
 * k_kbd_pending()/k_kbd_read() are FFI for the shell task.
 */
#include "kf_rt.h"

extern void isr_33(void);

#define KBD_BUF 256
static char     kbd_buf[KBD_BUF];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;
static int shift = 0;

static uint8_t kbd_inb(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void    kbd_outb(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }

/* PS/2 set-1 make codes 0x01..0x39 -> ASCII. */
static const char kbd_map[0x3A] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8',   /* 0x00-0x09 */
    '9', '0', '-', '=', '\b','\t','q', 'w', 'e', 'r',   /* 0x0A-0x13 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,    /* 0x14-0x1D */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',   /* 0x1E-0x27 */
    '\'','`',  0,  '\\','z', 'x', 'c', 'v', 'b', 'n',   /* 0x28-0x31 */
    'm', ',', '.', '/',  0,  '*',  0,  ' '              /* 0x32-0x39 */
};

void k_kbd_irq(void) {
    uint8_t sc = kbd_inb(0x60);
    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift = 0; return; }
    if (sc & 0x80) return;                       /* key release */
    char c = (sc < 0x3A) ? kbd_map[sc] : 0;
    if (!c) return;
    if (shift && c >= 'a' && c <= 'z') c -= 32;
    int next = (kbd_head + 1) % KBD_BUF;
    if (next != kbd_tail) { kbd_buf[kbd_head] = c; kbd_head = next; }
}

int64_t k_kbd_pending(void) { return (kbd_head - kbd_tail + KBD_BUF) % KBD_BUF; }

int64_t k_kbd_read(void) {
    if (kbd_head == kbd_tail) return 0;
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF;
    return (int64_t)(unsigned char)c;
}

/* Remap the PIC (IRQ0..15 -> 32..47), unmask IRQ1, point IDT[33] at isr_33. */
int64_t k_kbd_init(void) {
    uint8_t a1 = kbd_inb(0x21), a2 = kbd_inb(0xA1);
    kbd_outb(0x20, 0x11); kbd_outb(0xA0, 0x11);
    kbd_outb(0x21, 0x20); kbd_outb(0xA1, 0x28);
    kbd_outb(0x21, 0x04); kbd_outb(0xA1, 0x02);
    kbd_outb(0x21, 0x01); kbd_outb(0xA1, 0x01);
    kbd_outb(0x21, a1);   kbd_outb(0xA1, a2);
    kbd_outb(0x21, 0xFD);   /* unmask IRQ0 + IRQ1 (keyboard) only */
    kbd_outb(0xA1, 0xFF);   /* slave IRQs off */
    k_intr_set_gate(33, (int64_t)(uintptr_t)&isr_33);
    return 1;
}

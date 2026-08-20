/* kf_mouse.c — mouse for the desktop: USB tablet (absolute) or PS/2 fallback.
 *
 * Preferred path: QEMU usb-tablet over UHCI (kf_usb.c) — absolute positions,
 * so the gtk window does NOT grab the host cursor. Fallback: classic PS/2
 * mouse (relative 3-byte packets, requires cursor grab in QEMU).
 */
#include "kf_rt.h"

extern void isr_44(void);

static int mx = 640, my = 400;
static int mb = 0;
static int midx = 0;
static int p0 = 0, p1 = 0;
static int usb_mode = 0;         /* 1 = USB tablet active */

static uint8_t inb(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void    outb(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }

static void wait_in(void) { while (inb(0x64) & 0x02) ; }
static void wait_out(void) { while (!(inb(0x64) & 0x01)) ; }

void k_mouse_irq(void) {
    if (usb_mode) return;         /* USB path polls, no IRQ */
    uint8_t b = inb(0x60);
    if (midx == 0) {
        if (b & 0x08) { p0 = b; midx = 1; }
    } else if (midx == 1) {
        p1 = b; midx = 2;
    } else {
        int dx = (p1 & 0x80) ? p1 - 256 : p1;
        int dy = (b & 0x80) ? b - 256 : b;
        mx += dx; my -= dy;
        mb = p0 & 0x07;
        midx = 0;
        if (mx < 0) mx = 0; if (mx > 1279) mx = 1279;
        if (my < 0) my = 0; if (my > 799) my = 799;
    }
}

int64_t k_mouse_x(void) {
    if (usb_mode) return k_usb_tab_x();
    return mx;
}
int64_t k_mouse_y(void) {
    if (usb_mode) return k_usb_tab_y();
    return my;
}
int64_t k_mouse_buttons(void) {
    if (usb_mode) return k_usb_tab_btn();
    return mb;
}

int64_t k_mouse_init(void) {
    /* try USB tablet first (absolute pointer, no cursor grab) */
    if (k_usb_init()) {
        usb_mode = 1;
        return 1;
    }
    /* fallback: classic PS/2 mouse */
    wait_in(); outb(0x64, 0xA8);              /* enable aux device */
    wait_in(); outb(0x64, 0x20);              /* read command byte */
    wait_out(); uint8_t cmd = inb(0x60);
    cmd |= 0x02;                              /* enable aux IRQ (IRQ12) */
    cmd &= ~0x20;                             /* enable aux clock */
    wait_in(); outb(0x64, 0x60);              /* write command byte */
    wait_in(); outb(0x60, cmd);
    /* unmask IRQ12 on the slave PIC (bit 4 of 0xA1) */
    uint8_t m2 = inb(0xA1);
    outb(0xA1, m2 & ~0x10);
    wait_in(); outb(0x64, 0xD4); wait_in(); outb(0x60, 0xF4);  /* enable data reporting */
    k_intr_set_gate(44, (int64_t)(uintptr_t)&isr_44);
    return 1;
}

/* poll the USB tablet (no-op in PS/2 mode) — called each desktop tick */
int64_t k_mouse_poll(void) {
    if (usb_mode) return k_usb_poll();
    return 0;
}

/* input_a64.c — ввод aarch64: UART-консоль как клавиатура, мышь/USB — заглушки.
 *
 * Этап 1: команды шелла/desktop вводятся через PL011 RX (poll, без IRQ —
 * PS/2 на ARM не существует, USB-тач будет на этапе 2 через xHCI).
 * Контракты 1:1 с kf_kbd.c / kf_mouse.c / kf_usb.c / kf_gui.c:
 * kmain.kenga и desktop.kenga не меняются.
 */
#include "kf_rt.h"

/* --- клавиатура: PL011 RX -> ASCII-кольцо --- */
#define KBD_BUF 256
static char     kbd_buf[KBD_BUF];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;

#define UART0_FR (0x09000000ull + 0x018)
#define UART0_DR (0x09000000ull + 0x000)
#define FR_RXFE  (1u << 4)

static void kbd_drain(void) {
    while (!(mmio_read32(UART0_FR) & FR_RXFE)) {
        char c = (char)(mmio_read32(UART0_DR) & 0xFF);
        if (c == '\r') c = '\n';
        int next = (kbd_head + 1) % KBD_BUF;
        if (next != kbd_tail) { kbd_buf[kbd_head] = c; kbd_head = next; }
    }
}

int64_t k_kbd_init(void) { return 1; }   /* PL011 RX уже включён (hw_a64.c) */

int64_t k_kbd_pending(void) {
    kbd_drain();
    return (kbd_head - kbd_tail + KBD_BUF) % KBD_BUF;
}

int64_t k_kbd_read(void) {
    kbd_drain();
    if (kbd_head == kbd_tail) return 0;
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF;
    return (int64_t)(unsigned char)c;
}

/* --- мышь/тач: xHCI usb-tablet (usb_a64.c), как на x86 (kf_mouse) --- */
int64_t k_usb_init(void);
int64_t k_usb_poll(void);
int64_t k_usb_tab_x(void);
int64_t k_usb_tab_y(void);
int64_t k_usb_tab_btn(void);

static int usb_mode = 0;
static int mx = 400, my = 300, mbtn = 0;

int64_t k_mouse_init(void) {
    usb_mode = (int)k_usb_init();
    if (usb_mode) { mx = (int)k_usb_tab_x(); my = (int)k_usb_tab_y(); }
    return 1;
}
int64_t k_mouse_x(void)       { return mx; }
int64_t k_mouse_y(void)       { return my; }
int64_t k_mouse_buttons(void) { return mbtn; }
int64_t k_mouse_poll(void) {
    if (!usb_mode) return 0;
    if (k_usb_poll()) {
        mx = (int)k_usb_tab_x();
        my = (int)k_usb_tab_y();
        mbtn = (int)k_usb_tab_btn();
        return 1;
    }
    return 0;
}

/* --- USB: xHCI + usb-tablet — символы из usb_a64.c --- */
int64_t k_usb_init(void);
int64_t k_usb_poll(void);
int64_t k_usb_tab_x(void);
int64_t k_usb_tab_y(void);
int64_t k_usb_tab_btn(void);

/* --- GUI bridge (аналог kf_gui.c, без PIC-возни) --- */
int64_t k_gui_init(void) {
    k_kbd_init();
    k_fb_con_init();     /* kf_fb.c: очистить консоль */
    k_proc_init();       /* logger + agent + model агенты */
    k_mouse_init();
    k_arch_irq_enable(); /* daifclr — аналог sti: тикает timer */
    return 1;
}

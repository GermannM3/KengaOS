/* kf_gui.c — KengaOS 0.5: thin C bridge to the Kenga-written desktop.
 *
 * The desktop itself (init screen, event loop, sidebar, views, cursor) lives
 * in desktop.kenga / ui.kenga. This file only brings up the hardware the
 * desktop needs (keyboard, framebuffer console, process agents, PS/2 mouse)
 * and lets interrupts flow, then hands control to k_desktop_main().
 */
#include "kf_rt.h"

int64_t k_gui_init(void) {
    k_kbd_init();              /* PS/2 keyboard IRQ1 (vector 33) */
    k_fb_con_init();           /* clear screen */
    k_proc_init();             /* spawn logger + agent + model agents */
    k_mouse_init();            /* PS/2 mouse IRQ12 */
    uint8_t imr0;
    __asm__ __volatile__("inb %1, %0" : "=a"(imr0) : "Nd"((uint16_t)0x21));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)(imr0 & ~0x03)), "Nd"((uint16_t)0x21)); /* ensure IRQ0+IRQ1 unmasked */
    __asm__ __volatile__("sti");               /* enable interrupts (timer/kbd/mouse) */
    return 1;
}

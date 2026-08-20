/* k_power.c — reboot / shutdown (safe port I/O). */
#include "kf_rt.h"

static void outb(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }
static uint8_t inb(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

/* Reboot via the 8042 keyboard controller. */
int64_t k_power_reboot(void) {
    for (int i = 0; i < 10; i++) {
        outb(0x64, 0xFE);          /* 8042 reset pulse */
    }
    /* fallback: triple fault by loading a bogus IDT base is risky; just loop */
    for (;;) __asm__ __volatile__("hlt");
    return 0;
}

/* Best-effort shutdown: QEMU isa-debug-exit port (0xf4, value 0x31).
   On real hardware this needs ACPI; here it exits the QEMU process. */
int64_t k_power_shutdown(void) {
    outb(0xf4, 0x31);
    for (;;) __asm__ __volatile__("hlt");
    return 0;
}

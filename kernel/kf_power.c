/* k_power.c — reboot / shutdown (safe port I/O). */
#include "kf_rt.h"

static void outb(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }
static uint8_t inb(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

/* Reboot: сначала ACPI RESET_REG (kf_acpi.c), иначе 8042. */
int64_t k_power_reboot(void) {
    if (k_acpi_reboot()) { for (;;) __asm__ __volatile__("hlt"); }
    for (int i = 0; i < 10; i++) {
        outb(0x64, 0xFE);          /* 8042 reset pulse */
    }
    for (;;) __asm__ __volatile__("hlt");
    return 0;
}

/* Выключение: настоящий ACPI S5 (kf_acpi.c — PM1a/PM1b_CNT + SLP_EN).
   Только если ACPI нет — QEMU isa-debug-exit (порт 0xf4), это заглушка. */
int64_t k_power_shutdown(void) {
    if (k_acpi_ready()) {
        k_acpi_shutdown();
        for (;;) __asm__ __volatile__("hlt");
    }
    outb(0xf4, 0x31);
    for (;;) __asm__ __volatile__("hlt");
    return 0;
}

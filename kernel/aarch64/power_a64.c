/* power_a64.c — reboot/shutdown через PSCI (QEMU virt, и будущее ARM-железо).
 *
 * x86 делает 8042-контроллером и портом isa-debug-exit; на ARM это
 * PSCI-вызовы (SYSTEM_RESET / SYSTEM_OFF) — hvc #0, fn в x0.
 */
#include "kf_rt.h"

static void psci_call(uint64_t fn) {
    register uint64_t x0 __asm__("x0") = fn;
    __asm__ __volatile__("hvc #0"
                         : : "r"(x0)
                         : "x1", "x2", "x3", "memory");
}

int64_t k_power_reboot(void) {
    psci_call(0x84000009);   /* PSCI SYSTEM_RESET */
    for (;;) k_arch_hlt();
    return 0;
}

int64_t k_power_shutdown(void) {
    psci_call(0x84000008);   /* PSCI SYSTEM_OFF */
    for (;;) k_arch_hlt();
    return 0;
}

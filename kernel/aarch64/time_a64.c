/* time_a64.c — generic timer (CNTV) вместо PIT.
 *
 * IRQ виртуального таймера — PPI 27 (INTID 27 на GICv2 QEMU virt),
 * обрабатывается в intr_a64.c (k_a64_irq_entry -> k_timer_tick).
 * Тик 10 мс — тот же контракт, что PIT-версия kf_time.c.
 */
#include "kf_rt.h"

static volatile uint64_t ticks = 0;
static uint64_t interval = 0;

void k_timer_tick(void) {
    ticks++;
}

/* CNTV — down-counter: после нуля надо перезарядить TVAL, иначе
   прерывание уровня держится и штормит GIC. Зовётся из k_a64_irq_c
   перед EOI. */
void k_timer_rearm(void) {
    if (interval) {
        __asm__ __volatile__("msr cntv_tval_el0, %0" : : "r"(interval));
    }
}

int64_t k_time_uptime_ms(void) { return (int64_t)(ticks * 10); }

int64_t k_timer_init(void) {
    uint64_t frq = 0;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(frq));
    if (frq == 0) frq = 62500000;   /* разумный дефолт для virt */

    interval = frq / 100;           /* 10 мс */
    __asm__ __volatile__("msr cntv_tval_el0, %0" : : "r"(interval));
    __asm__ __volatile__("msr cntv_ctl_el0, %0"
                         : : "r"((uint64_t)1));  /* ENABLE, не маскирован */
    return 1;
}

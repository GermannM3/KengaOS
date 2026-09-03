/* kf_time.c — PIT timer + uptime counter (no context switch).
 *
 * IRQ0 -> vector 32. The timer ISR simply increments a tick counter and EOIs,
 * returning to the same task via iretq. k_time_uptime_ms() reports uptime.
 * (Preemptive task switching on this timer is a separate, unfinished feature.)
 */
#include "kf_rt.h"

extern void isr_32(void);

static volatile uint64_t ticks = 0;

void k_timer_tick(void) {
    ticks++;
}

int64_t k_time_uptime_ms(void) { return (int64_t)(ticks * 10); }

int64_t k_timer_init(void) {
    /* гейт IRQ0 — ДО размаскировки PIC: PIT уже тикает с прошивки,
       IRQ0 между unmask и set_gate попадал в мусорный вектор (маскировался
       под DIVIDE BY ZERO через isr_0-dead-gate) */
    k_intr_set_gate(32, (int64_t)(uintptr_t)&isr_32);
    uint8_t a1, a2;
    __asm__ __volatile__("inb %1, %0" : "=a"(a1) : "Nd"((uint16_t)0x21));
    __asm__ __volatile__("inb %1, %0" : "=a"(a2) : "Nd"((uint16_t)0xA1));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0x20));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0xA0));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0x21));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x28), "Nd"((uint16_t)0xA1));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x04), "Nd"((uint16_t)0x21));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x02), "Nd"((uint16_t)0xA1));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x01), "Nd"((uint16_t)0x21));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x01), "Nd"((uint16_t)0xA1));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)a1), "Nd"((uint16_t)0x21));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)a2), "Nd"((uint16_t)0xA1));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0xFC), "Nd"((uint16_t)0x21));   /* IRQ0+IRQ1 */
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0xFF), "Nd"((uint16_t)0xA1));
    uint16_t div = (uint16_t)(1193182 / 100);
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0x36), "Nd"((uint16_t)0x43));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)(div & 0xFF)), "Nd"((uint16_t)0x40));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)((div >> 8) & 0xFF)), "Nd"((uint16_t)0x40));
    return 1;
}

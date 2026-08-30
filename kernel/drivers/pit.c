/*  KengaOS — PIT (8254 timer) драйвер.
    Канал 0 → IRQ0, генерирует прерывание с заданной частотой.
*/
#include "pit.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/idt.h"

#define PIT_BASE       0x40
#define PIT_CH0        (PIT_BASE + 0)
#define PIT_CMD        (PIT_BASE + 3)

#define PIT_CMD_BINARY 0x00
#define PIT_CMD_MODE3  0x06   /* square wave generator */
#define PIT_CMD_LH     0x30   /* lobyte/hibyte access */
#define PIT_CMD_CH0    0x00

static volatile u64 ticks = 0;

static void pit_callback(void *ctx) {
    (void)ctx;
    ticks++;
}

void pit_init(u32 frequency) {
    irq_register(0, pit_callback);

    /* Базовая частота PIT = 1193182 Гц */
    u32 divisor = 1193182 / frequency;
    if (divisor == 0) divisor = 1;

    outb(PIT_CMD, PIT_CMD_BINARY | PIT_CMD_MODE3 | PIT_CMD_LH | PIT_CMD_CH0);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
}

u64 pit_ticks(void) {
    return ticks;
}

void pit_sleep_ms(u64 ms) {
    u64 start = ticks;
    while ((ticks - start) < ms) {
        __asm__ volatile ("hlt");
    }
}

/*  KengaOS — PIT (Programmable Interval Timer) 8253/8254.
    Используется канал 0 для таймерных тиков. Частота ~1000 Гц.
*/
#ifndef KENGA_PIT_H
#define KENGA_PIT_H

#include "../lib/types.h"

#define PIT_FREQUENCY 1000   /* 1000 Гц = 1 мс на тик */

void pit_init(u32 frequency);
u64 pit_ticks(void);
void pit_sleep_ms(u64 ms);

#endif

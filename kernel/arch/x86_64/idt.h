/*  KengaOS — IDT и обработчики исключений/прерываний.
    Минимальный набор: 32 исключения CPU + IRQ0..15 (на PIC).
*/
#ifndef KENGA_IDT_H
#define KENGA_IDT_H

#include "../lib/types.h"

void idt_init(void);

/* Зарегистрировать пользовательский обработчик IRQ (32..47). */
typedef void (*irq_handler_t)(void *ctx);
void irq_register(u8 irq_num, irq_handler_t handler);

#endif

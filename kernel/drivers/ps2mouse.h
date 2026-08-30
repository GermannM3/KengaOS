/*  KengaOS — PS/2 мышь (i8042 auxiliary port).
    IRQ12, пакеты по 3 байта. Отдаёт позицию курсора и кнопки.
*/
#ifndef KENGA_PS2MOUSE_H
#define KENGA_PS2MOUSE_H

#include "../lib/types.h"

void ps2mouse_init(void);

/* Позиция курсора (пиксели экрана). */
u32 mouse_x(void);
u32 mouse_y(void);

/* Битовая маска кнопок: bit0 = left, bit1 = right, bit2 = middle. */
u32 mouse_buttons(void);

#endif

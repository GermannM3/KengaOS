/*  KengaOS — клавиатура PS/2 (i8042).
    Простая раскладка US (QWERTY). Для русской раскладки потребуется
    дополнительная таблица — покаShell принимает латиницу, а вывод может
    быть на любом языке (i18n).
*/
#ifndef KENGA_KBD_H
#define KENGA_KBD_H

#include "../lib/types.h"

void kbd_init(void);

/* Получить следующий символ (блокирующий). Возвращает 0 если ничего нет. */
char kbd_getc(void);

/* Неблокирующая проверка */
bool kbd_has_char(void);

/* Переключить раскладку. 0 = US (QWERTY), 1 = RU (ЙЦУКЕН). */
void kbd_set_layout(int layout);
int  kbd_get_layout(void);

/* Переключить раскладку по ToggleKey (для использования из shell). */
void kbd_toggle_layout(void);

#endif

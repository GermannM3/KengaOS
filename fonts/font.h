/*  KengaOS — шрифт 8x16 с поддержкой латиницы и кириллицы.
    Сгенерирован из public-domain bitmap font (восходит к IBM PC).
    Поддерживаемые диапазоны:
      - U+0020 .. U+007F   (ASCII — латиница, цифры, знаки)
      - U+00A0 .. U+00FF   (Latin-1 — псевдографика, диакритика)
      - U+0400 .. U+04FF   (Кириллица — русский, украинский, белорусский, сербский...)

    Если символа нет — выводится '?'.
*/
#ifndef KENGA_FONT_H
#define KENGA_FONT_H

#include "../lib/types.h"

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

/* Получить bitmap-строку (16 байт) для символа UTF-8.
   Возвращает NULL если символ не поддерживается. */
const u8 *font_get_glyph(u32 codepoint);

#endif

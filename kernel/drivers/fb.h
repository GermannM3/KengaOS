/*  KengaOS — Framebuffer driver.
    Прямая запись в память, выданную Limine (UEFI GOP / BIOS VBE).
    Поддержка: 32-bpp (BGRA8 / BGRX8), 24-bpp, 16-bpp (RGB565).
    + простая консоль с переносом строк и скроллингом.
*/
#ifndef KENGA_FB_H
#define KENGA_FB_H

#include "../lib/types.h"
#include "../arch/x86_64/limine.h"
#include "../../fonts/ui_font.h"

#define FB_COLOR_BLACK   0x000000
#define FB_COLOR_WHITE   0xFFFFFF
#define FB_COLOR_RED     0xFF0000
#define FB_COLOR_GREEN   0x00FF00
#define FB_COLOR_BLUE    0x0000FF
#define FB_COLOR_YELLOW  0xFFFF00
#define FB_COLOR_CYAN    0x00FFFF
#define FB_COLOR_MAGENTA 0xFF00FF
#define FB_COLOR_GREY    0x808080
#define FB_COLOR_LIGHT_GREY 0xC0C0C0
#define FB_COLOR_DARK_BLUE   0x000080

void fb_init(struct limine_framebuffer *fb);
void fb_clear(u32 color);

/* Прямой пиксель */
void fb_put_pixel(u32 x, u32 y, u32 color);

/* Рисование прямоугольника */
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);

/* Консольный вывод (UTF-8 строки) */
void fb_set_text_color(u32 fg, u32 bg);
void fb_putc(char c);
void fb_puts(const char *utf8);

/* Позиция курсора */
void fb_set_cursor(u32 col, u32 row);
void fb_get_cursor(u32 *col, u32 *row);

/* Размер консоли в символах */
u32 fb_cols(void);
u32 fb_rows(void);

/* Разрешение framebuffer в пикселях. */
void fb_get_size(u32 *w, u32 *h);

/* Указатель на начало видеопамяти (для кэширования сцен). */
void *fb_mem_ptr(void);

/* ============================================================
   Рендер-примитивы для UI (дизайн-система KengaOS 0.5)
   Все цвета — 0xRRGGBB; alpha 0..255 (0 = прозрачно).
   ============================================================ */

/* Один пиксель с альфа-смешиванием. */
void fb_px(u32 x, u32 y, u32 rgb, u8 alpha);

/* Прямоугольник с альфа-смешиванием поверх фона. */
void fb_blend_rect(u32 x, u32 y, u32 w, u32 h, u32 rgb, u8 alpha);

/* Вертикальный градиент c1 → c2 с альфой. */
void fb_gradient_v(u32 x, u32 y, u32 w, u32 h, u32 rgb1, u32 rgb2, u8 alpha);

/* Скруглённый прямоугольник с альфой. */
void fb_round_rect(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 rgb, u8 alpha);

/* Контур скруглённого прямоугольника толщиной 1px. */
void fb_round_rect_outline(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 rgb, u8 alpha);

/* Круг с радиальным затуханием (glow-эффект). */
void fb_glow(u32 cx, u32 cy, u32 radius, u32 rgb, u8 alpha_max);

/* Заливка круга. */
void fb_circle(u32 cx, u32 cy, u32 radius, u32 rgb, u8 alpha);

/* Текст с прозрачным фоном (только глифы, альфа-блендинг). */
void fb_text(u32 x, u32 y, const char *utf8, u32 rgb, u8 alpha);

/* ── UI v2 ─────────────────────────────────────────────── */
/* Перехват консольного вывода fb_putc (терминальное окно). */
void fb_set_console_hook(void (*fn)(const char *s, u32 len));

/* Блит альфа-карты (покров 0..255, stride — ширина источника). */
void fb_blit_alpha(const u8 *cov, u32 sw, u32 sh, u32 stride,
                   u32 dx, u32 dy, u32 rgb, u8 alpha);

/* AA-текст из атласа ui_font; возвращает ширину строки. */
u32 fb_text_k(const kfont_t *f, u32 x, u32 y, const char *utf8,
              u32 rgb, u8 alpha);
u32 fb_text_k_width(const kfont_t *f, const char *utf8);

/* Ширина строки в пикселях (для центрирования). */
u32 fb_text_width(const char *utf8);

#endif

/* Рисование в кэш-буфер вместо экрана (атомарный кадр). */
void fb_set_backbuffer(void *p);
void *fb_target(void);

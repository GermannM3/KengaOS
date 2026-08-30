/*  KengaOS — Framebuffer driver и простая текстовая консоль.
    Поддержка UTF-8 (многобайтные последовательности).
    Порядок каналов 32-bpp берём из масок Limine (QEMU std-vga — XRGB,
    UEFI GOP часто XBGR) и кэшируем в fb_r_shift/fb_b_shift.
*/
#include "fb.h"
#include "../arch/x86_64/limine.h"
#include "../arch/x86_64/io.h"
#include "../lib/types.h"
#include "../../fonts/font.h"

static struct limine_framebuffer *fb = NULL;
static u8 *fb_mem = NULL;
static u32 fb_width = 0;
static u32 fb_height = 0;
static u32 fb_pitch = 0;
static u32 fb_bpp = 0;
static u32 fb_bytes_per_pixel = 0;
static u8 fb_r_shift = 0;   /* позиция красного канала в dword (32-bpp) */
static u8 fb_b_shift = 16;  /* позиция синего  — по умолчанию XRGB */
/* Бэкбуфер: если установлен, примитивы пишут в него (атомарный кадр). */
static u8 *back_buf = NULL;

static u32 cursor_col = 0;
static u32 cursor_row = 0;

/* Перехват консольного вывода (терминальное окно UI) — определён внизу. */
static void (*con_hook)(const char *s, u32 len) = NULL;
static u32 text_cols = 0;
static u32 text_rows = 0;

static u32 text_fg = FB_COLOR_LIGHT_GREY;
static u32 text_bg = FB_COLOR_BLACK;

/* Конвертировать RGB (0xRRGGBB) → формат framebuffer */
static u32 fb_pack(u32 rgb) {
    u8 r = (rgb >> 16) & 0xFF;
    u8 g = (rgb >> 8) & 0xFF;
    u8 b = rgb & 0xFF;
    if (fb_bpp == 32) {
        /* Порядок каналов из масок Limine (XRGB или XBGR) */
        return (0xFFu << 24) | (r << fb_r_shift) | (g << 8) | (b << fb_b_shift);
    } else if (fb_bpp == 24) {
        return (r << 16) | (g << 8) | b;
    } else if (fb_bpp == 16) {
        /* RGB565 */
        u32 c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        return c;
    }
    return rgb;
}

static void fb_write_pixel(u32 x, u32 y, u32 packed) {
    if (x >= fb_width || y >= fb_height) return;
    u8 *tgt = back_buf ? back_buf : fb_mem;
    u8 *addr = tgt + y * fb_pitch + x * fb_bytes_per_pixel;
    if (fb_bpp == 32) {
        *(u32*)addr = packed;
    } else if (fb_bpp == 24) {
        addr[0] = packed & 0xFF;
        addr[1] = (packed >> 8) & 0xFF;
        addr[2] = (packed >> 16) & 0xFF;
    } else if (fb_bpp == 16) {
        *(u16*)addr = (u16)packed;
    }
}

void fb_init(struct limine_framebuffer *limine_fb) {
    fb = limine_fb;
    fb_mem = (u8*)fb->address;
    fb_width  = fb->width;
    fb_height = fb->height;
    fb_pitch  = fb->pitch;
    fb_bpp    = fb->bpp;
    fb_bytes_per_pixel = fb_bpp / 8;
    if (fb_bpp == 32) {
        if (fb->red_mask_size == 8 && fb->blue_mask_size == 8) {
            fb_r_shift = fb->red_mask_shift;
            fb_b_shift = fb->blue_mask_shift;
        } else {
            fb_r_shift = 0; fb_b_shift = 16;  /* XRGB (VBE/QEMU) */
        }
    }

    text_cols = fb_width  / FONT_WIDTH;
    text_rows = fb_height / FONT_HEIGHT;
    cursor_col = 0;
    cursor_row = 0;

    fb_clear(FB_COLOR_BLACK);
}

void fb_clear(u32 color) {
    u32 packed = fb_pack(color);
    for (u32 y = 0; y < fb_height; y++) {
        for (u32 x = 0; x < fb_width; x++) {
            fb_write_pixel(x, y, packed);
        }
    }
    cursor_col = 0;
    cursor_row = 0;
}

void fb_put_pixel(u32 x, u32 y, u32 color) {
    fb_write_pixel(x, y, fb_pack(color));
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    u32 packed = fb_pack(color);
    for (u32 dy = 0; dy < h; dy++) {
        for (u32 dx = 0; dx < w; dx++) {
            fb_write_pixel(x + dx, y + dy, packed);
        }
    }
}

void fb_set_text_color(u32 fg, u32 bg) {
    text_fg = fg;
    text_bg = bg;
}

u32 fb_cols(void) { return text_cols; }
u32 fb_rows(void) { return text_rows; }

void fb_get_size(u32 *w, u32 *h) {
    if (w) *w = fb_width;
    if (h) *h = fb_height;
}

void *fb_mem_ptr(void) { return fb_mem; }

void fb_set_cursor(u32 col, u32 row) {
    if (col < text_cols) cursor_col = col;
    if (row < text_rows) cursor_row = row;
}

void fb_get_cursor(u32 *col, u32 *row) {
    if (col) *col = cursor_col;
    if (row) *row = cursor_row;
}

static void fb_scroll_up(void) {
    /* Сдвинуть все строки вверх на одну */
    u8 *dst = fb_mem;
    u8 *src = fb_mem + FONT_HEIGHT * fb_pitch;
    u32 bytes = (text_rows - 1) * FONT_HEIGHT * fb_pitch;
    for (u32 i = 0; i < bytes; i++) {
        dst[i] = src[i];
    }
    /* Очистить последнюю строку */
    u32 packed = fb_pack(text_bg);
    u8 *last = fb_mem + (text_rows - 1) * FONT_HEIGHT * fb_pitch;
    for (u32 y = 0; y < FONT_HEIGHT; y++) {
        for (u32 x = 0; x < fb_width; x++) {
            u8 *addr = last + y * fb_pitch + x * fb_bytes_per_pixel;
            if (fb_bpp == 32) *(u32*)addr = packed;
            else if (fb_bpp == 24) { addr[0]=packed; addr[1]=packed>>8; addr[2]=packed>>16; }
            else if (fb_bpp == 16) *(u16*)addr = (u16)packed;
        }
    }
}

static void fb_draw_glyph(u32 codepoint) {
    const u8 *glyph = font_get_glyph(codepoint);
    if (!glyph) return;

    u32 base_x = cursor_col * FONT_WIDTH;
    u32 base_y = cursor_row * FONT_HEIGHT;

    u32 fg_packed = fb_pack(text_fg);
    u32 bg_packed = fb_pack(text_bg);

    for (u32 gy = 0; gy < FONT_HEIGHT; gy++) {
        u8 row = glyph[gy];
        for (u32 gx = 0; gx < FONT_WIDTH; gx++) {
            u32 color = (row & (0x80 >> gx)) ? fg_packed : bg_packed;
            u8 *addr = fb_mem + (base_y + gy) * fb_pitch + (base_x + gx) * fb_bytes_per_pixel;
            if (fb_bpp == 32) *(u32*)addr = color;
            else if (fb_bpp == 24) { addr[0]=color; addr[1]=color>>8; addr[2]=color>>16; }
            else if (fb_bpp == 16) *(u16*)addr = (u16)color;
        }
    }
}

/* Декодер UTF-8: читает многобайтные последовательности. */
static u32 utf8_pending = 0;
static u32 utf8_codepoint = 0;

static void fb_emit_codepoint(u32 cp) {
    if (cp == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= text_rows) {
            fb_scroll_up();
            cursor_row = text_rows - 1;
        }
        return;
    }
    if (cp == '\r') { cursor_col = 0; return; }
    if (cp == '\t') {
        cursor_col = (cursor_col + 4) & ~3u;
        if (cursor_col >= text_cols) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= text_rows) {
                fb_scroll_up();
                cursor_row = text_rows - 1;
            }
        }
        return;
    }
    if (cp == 0x08) {  /* backspace */
        if (cursor_col > 0) cursor_col--;
        return;
    }

    fb_draw_glyph(cp);
    cursor_col++;
    if (cursor_col >= text_cols) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= text_rows) {
            fb_scroll_up();
            cursor_row = text_rows - 1;
        }
    }
}

void fb_putc(char c) {
    /* Перехват: консольный вывод идёт в терминальное окно UI. */
    if (con_hook) {
        con_hook(&c, 1);
        return;
    }
    u8 b = (u8)c;
    if (b < 0x80) {
        utf8_pending = 0;
        utf8_codepoint = 0;
        fb_emit_codepoint(b);
    } else if ((b & 0xE0) == 0xC0) {
        utf8_pending = 1;
        utf8_codepoint = b & 0x1F;
    } else if ((b & 0xF0) == 0xE0) {
        utf8_pending = 2;
        utf8_codepoint = b & 0x0F;
    } else if ((b & 0xF8) == 0xF0) {
        utf8_pending = 3;
        utf8_codepoint = b & 0x07;
    } else if ((b & 0xC0) == 0x80 && utf8_pending > 0) {
        utf8_codepoint = (utf8_codepoint << 6) | (b & 0x3F);
        utf8_pending--;
        if (utf8_pending == 0) {
            fb_emit_codepoint(utf8_codepoint);
            utf8_codepoint = 0;
        }
    } else {
        /* невалидный байт */
        utf8_pending = 0;
        fb_emit_codepoint('?');
    }
}

void fb_puts(const char *utf8) {
    while (*utf8) {
        fb_putc(*utf8);
        utf8++;
    }
}

/* ============================================================
   Рендер-примитивы (alpha blending, 32-bpp fast path)
   ============================================================ */

/* Прочитать пиксель как 0x00RRGGBB (альфа игнорируется). */
static u32 fb_read_pixel_rgb(u32 x, u32 y) {
    if (x >= fb_width || y >= fb_height) return 0;
    u8 *tgt = back_buf ? back_buf : fb_mem;
    u8 *addr = tgt + y * fb_pitch + x * fb_bytes_per_pixel;
    if (fb_bpp == 32) {
        u32 v = *(u32*)addr;
        return ((v >> fb_r_shift) & 0xFF) << 16 | ((v >> 8) & 0xFF) << 8
             | ((v >> fb_b_shift) & 0xFF);
    } else if (fb_bpp == 24) {
        return addr[2] << 16 | addr[1] << 8 | addr[0];
    }
    return 0;
}

/* Записать смешанный пиксель: out = src*a + dst*(1-a). */
static void fb_blend_pixel(u32 x, u32 y, u32 rgb, u32 a256) {
    if (x >= fb_width || y >= fb_height) return;
    u32 dr, dg, db;
    u8 *tgt = back_buf ? back_buf : fb_mem;
    u8 *addr = tgt + y * fb_pitch + x * fb_bytes_per_pixel;
    if (fb_bpp == 32) {
        u32 v = *(u32*)addr;
        dr = (v >> fb_r_shift) & 0xFF; dg = (v >> 8) & 0xFF; db = (v >> fb_b_shift) & 0xFF;
    } else if (fb_bpp == 24) {
        dr = addr[2]; dg = addr[1]; db = addr[0];
    } else {
        return; /* 16-bpp блендинг не поддерживаем */
    }
    u32 sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    u32 r = (sr * a256 + dr * (255 - a256)) / 255;
    u32 g = (sg * a256 + dg * (255 - a256)) / 255;
    u32 b = (sb * a256 + db * (255 - a256)) / 255;
    if (fb_bpp == 32) {
        *(u32*)addr = 0xFF000000u | (r << fb_r_shift) | (g << 8) | (b << fb_b_shift);
    } else {
        addr[0] = (u8)b; addr[1] = (u8)g; addr[2] = (u8)r;
    }
}

void fb_px(u32 x, u32 y, u32 rgb, u8 alpha) { fb_blend_pixel(x, y, rgb, alpha); }

void fb_blend_rect(u32 x, u32 y, u32 w, u32 h, u32 rgb, u8 alpha) {
    for (u32 dy = 0; dy < h; dy++)
        for (u32 dx = 0; dx < w; dx++)
            fb_blend_pixel(x + dx, y + dy, rgb, alpha);
}

void fb_gradient_v(u32 x, u32 y, u32 w, u32 h, u32 rgb1, u32 rgb2, u8 alpha) {
    for (u32 dy = 0; dy < h; dy++) {
        /* Линейная интерполяция цвета по вертикали. */
        u32 t = (h > 1) ? (dy * 255) / (h - 1) : 0;
        u32 r1 = (rgb1 >> 16) & 0xFF, g1 = (rgb1 >> 8) & 0xFF, b1 = rgb1 & 0xFF;
        u32 r2 = (rgb2 >> 16) & 0xFF, g2 = (rgb2 >> 8) & 0xFF, b2 = rgb2 & 0xFF;
        u32 rgb = ((r1 + (r2 - r1) * t / 255) << 16)
                | ((g1 + (g2 - g1) * t / 255) << 8)
                | (b1 + (b2 - b1) * t / 255);
        for (u32 dx = 0; dx < w; dx++)
            fb_blend_pixel(x + dx, y + dy, rgb, alpha);
    }
}

/* Внутри ли точка скруглённого прямоугольника. */
static int round_contains(u32 px, u32 py, u32 x, u32 y, u32 w, u32 h, u32 r) {
    if (px < x || py < y || px >= x + w || py >= y + h) return 0;
    if (r == 0) return 1;
    u32 cx = px, cy = py;
    /* Определяем угловую зону и считаем расстояние до центра дуги. */
    if (px < x + r && py < y + r)            { cx = x + r; cy = y + r; }
    else if (px >= x + w - r && py < y + r)  { cx = x + w - 1 - r; cy = y + r; }
    else if (px < x + r && py >= y + h - r)  { cx = x + r; cy = y + h - 1 - r; }
    else if (px >= x + w - r && py >= y + h - r) { cx = x + w - 1 - r; cy = y + h - 1 - r; }
    else return 1;
    i64 dx = (i64)px - cx, dyy = (i64)py - cy;
    return (dx * dx + dyy * dyy) <= (i64)r * (i64)r;
}

void fb_round_rect(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 rgb, u8 alpha) {
    u32 r = radius;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (u32 dy = 0; dy < h; dy++)
        for (u32 dx = 0; dx < w; dx++)
            if (round_contains(x + dx, y + dy, x, y, w, h, r))
                fb_blend_pixel(x + dx, y + dy, rgb, alpha);
}

void fb_round_rect_outline(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 rgb, u8 alpha) {
    u32 r = radius;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (u32 dy = 0; dy < h; dy++)
        for (u32 dx = 0; dx < w; dx++) {
            int in  = round_contains(x + dx, y + dy, x, y, w, h, r);
            int in2 = round_contains(x + dx, y + dy, x + 1, y + 1,
                                     (w > 2) ? w - 2 : 1, (h > 2) ? h - 2 : 1, r);
            if (in && !in2)
                fb_blend_pixel(x + dx, y + dy, rgb, alpha);
        }
}

void fb_circle(u32 cx, u32 cy, u32 radius, u32 rgb, u8 alpha) {
    i64 r = radius;
    for (i64 dy = -(i64)radius; dy <= (i64)radius; dy++)
        for (i64 dx = -(i64)radius; dx <= (i64)radius; dx++)
            if (dx * dx + dy * dy <= r * r)
                fb_blend_pixel((u32)((i64)cx + dx), (u32)((i64)cy + dy), rgb, alpha);
}

void fb_glow(u32 cx, u32 cy, u32 radius, u32 rgb, u8 alpha_max) {
    i64 r = radius;
    for (i64 dy = -(i64)radius; dy <= (i64)radius; dy++) {
        for (i64 dx = -(i64)radius; dx <= (i64)radius; dx++) {
            i64 d2 = dx * dx + dy * dy;
            if (d2 > r * r) continue;
            /* Квадратичное затухание к краю. */
            u32 a = (u32)((u64)alpha_max * (u64)(r * r - d2) / ((u64)r * r));
            fb_blend_pixel((u32)((i64)cx + dx), (u32)((i64)cy + dy), rgb, (u8)a);
        }
    }
}

u32 fb_text_width(const char *utf8) {
    u32 n = 0;
    while (*utf8) {
        u8 b = (u8)*utf8;
        if (b < 0x80) { utf8++; n++; }
        else if ((b & 0xE0) == 0xC0) { utf8 += 2; n++; }
        else if ((b & 0xF0) == 0xE0) { utf8 += 3; n++; }
        else if ((b & 0xF8) == 0xF0) { utf8 += 4; n++; }
        else utf8++;
    }
    return n * FONT_WIDTH;
}

/* Нарисовать один глиф с альфой (прозрачный фон). */
static void fb_draw_glyph_alpha(u32 base_x, u32 base_y, u32 codepoint, u32 rgb, u8 alpha) {
    const u8 *glyph = font_get_glyph(codepoint);
    if (!glyph) glyph = font_get_glyph('?');
    if (!glyph) return;
    for (u32 gy = 0; gy < FONT_HEIGHT; gy++) {
        u8 row = glyph[gy];
        for (u32 gx = 0; gx < FONT_WIDTH; gx++) {
            if (row & (0x80 >> gx))
                fb_blend_pixel(base_x + gx, base_y + gy, rgb, alpha);
        }
    }
}

/* Декодер UTF-8 → кодпоинт (без состояния — для полной строки). */
static const char *utf8_next(const char *s, u32 *cp) {
    u8 b = (u8)*s;
    if (b < 0x80) { *cp = b; return s + 1; }
    u32 len = 0, val = 0;
    if ((b & 0xE0) == 0xC0) { len = 2; val = b & 0x1F; }
    else if ((b & 0xF0) == 0xE0) { len = 3; val = b & 0x0F; }
    else if ((b & 0xF8) == 0xF0) { len = 4; val = b & 0x07; }
    else { *cp = '?'; return s + 1; }
    s += 1;
    for (u32 i = 1; i < len; i++) {
        u8 c = (u8)*s;
        if ((c & 0xC0) != 0x80) { *cp = '?'; return s; }
        val = (val << 6) | (c & 0x3F);
        s++;
    }
    *cp = val;
    return s;
}

void fb_text(u32 x, u32 y, const char *utf8, u32 rgb, u8 alpha) {
    u32 cx = x;
    while (*utf8) {
        u32 cp;
        utf8 = utf8_next(utf8, &cp);
        fb_draw_glyph_alpha(cx, y, cp, rgb, alpha);
        cx += FONT_WIDTH;
    }
}

/* ============================================================
   UI v2: альфа-спрайты, kfont-текст, перехват консоли
   ============================================================ */
#include "../../fonts/ui_font.h"

/* Перехват текстовой консоли: если установлен, fb_putc уходит в хук
   (терминальное окно), а не на экран. */
void fb_set_console_hook(void (*fn)(const char *s, u32 len)) { con_hook = fn; }
void *fb_hook_target(void) { return (void*)con_hook; }

/* Блит альфа-карты (спрайт глоу / глиф из атласа).
   cov: покров 0..255, stride — ширина строки источника. */
void fb_blit_alpha(const u8 *cov, u32 sw, u32 sh, u32 stride,
                   u32 dx, u32 dy, u32 rgb, u8 alpha) {
    u32 sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    for (u32 y = 0; y < sh; y++) {
        u32 py = dy + y;
        if (py >= fb_height) break;
        const u8 *src = cov + y * stride;
        for (u32 x = 0; x < sw; x++) {
            u32 cov2 = (src[x] * alpha) / 255;
            if (!cov2) continue;
            u32 px = dx + x;
            if (px >= fb_width) break;
            fb_blend_pixel(px, py, rgb, (u8)cov2);
        }
    }
    (void)sr; (void)sg; (void)sb;
}

/* Текст AA-шрифтом. Возвращает ширину отрисованной строки. */
u32 fb_text_k(const kfont_t *f, u32 x, u32 y, const char *utf8,
              u32 rgb, u8 alpha) {
    u32 cx = x;
    while (*utf8) {
        u32 cp;
        utf8 = utf8_next(utf8, &cp);
        /* перенос строки и таб не поддерживаем в kfont — это для меток */
        if (cp == '\n' || cp == '\r') continue;
        const kglyph_t *g = kf_lookup(f, cp);
        if (!g) g = kf_lookup(f, '?');
        if (!g) continue;
        fb_blit_alpha(f->alpha + g->v * f->w + g->u, g->w, g->h, f->w,
                      (u32)((i64)cx + g->ox), (u32)((i64)y + g->oy), rgb, alpha);
        cx += g->adv;
    }
    return cx - x;
}

/* Ширина строки без отрисовки. */
u32 fb_text_k_width(const kfont_t *f, const char *utf8) {
    u32 w = 0;
    while (*utf8) {
        u32 cp;
        utf8 = utf8_next(utf8, &cp);
        const kglyph_t *g = kf_lookup(f, cp);
        if (!g) g = kf_lookup(f, '?');
        if (!g) continue;
        w += g->adv;
    }
    return w;
}

void fb_set_backbuffer(void *p) { back_buf = (u8 *)p; }
void *fb_target(void) { return back_buf ? (void *)back_buf : (void *)fb_mem; }

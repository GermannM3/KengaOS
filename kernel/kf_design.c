/* ═══════════════════════════════════════════════════════════════════════
   kernel/kf_design.c — дизайн-система KengaOS 0.6 "Aurora"
   Графические примитивы для реального framebuffer ядра.
   ───────────────────────────────────────────────────────────────────────
   ИНТЕГРАЦИЯ:
   1) Положите файл в kernel/
   2) В scripts/build.sh добавьте к этапу компиляции C:
        $CC $CFLAGS -c kernel/kf_design.c -o build/kf_design.o
      и добавьте build/kf_design.o в команду линковки ld.lld
      (рядом с kf_fb.o / kf_mem.o и т.д.)
   3) СОГЛАСУЙТЕ 3 символа ниже с вашим kf_fb.c:
        fb, fb_w, fb_h, fb_pitch  — база и геометрия framebuffer
        fb_puts(x,y,s,fg)         — ваша функция вывода UTF-8 строки
      Если имена другие — поправьте блок "МАППИНГ" (weak-символы
      позволяют файлу собраться в любом случае, потом подмените).
   4) Формат пикселя: 32 bpp XRGB (стандарт Limine). Если у вас BGR —
      цвета поменяются местами, добавьте swap в blend().
   ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

/* ── МАППИНГ под kf_fb.c (поправить имена при необходимости) ─────── */
__attribute__((weak)) uint32_t* fb = 0;            /* база framebuffer */
__attribute__((weak)) int fb_w = 1280;             /* ширина, px       */
__attribute__((weak)) int fb_h = 800;              /* высота, px       */
__attribute__((weak)) int fb_pitch = 1280;         /* pitch, px        */
__attribute__((weak)) void fb_puts(int x, int y, const char* s, uint32_t fg)
{ (void)x; (void)y; (void)s; (void)fg; }           /* подменить! */
/* ──────────────────────────────────────────────────────────────────── */

static inline uint32_t* PX(int x, int y)
{ return fb + (uintptr_t)y * (uintptr_t)fb_pitch + (uintptr_t)x; }

static inline int inb(int x, int y)
{ return fb && x >= 0 && y >= 0 && x < fb_w && y < fb_h; }

/* ── палитра (единая для ядра) ── */
#define K_BG      0x070b14u
#define K_GLASS   0x131b30u
#define K_TEXT    0xe8ecf8u
#define K_DIM     0x8f9ab5u
#define K_ACC     0x8b7bffu
#define K_ACC2    0x22d3eeu
#define K_OK      0x3ddc84u
#define K_WARN    0xfebc2eu
#define K_ERR     0xff5f57u

/* ══════════ БАЗОВЫЕ ПРИМИТИВЫ ══════════ */

void d_px(int x, int y, uint32_t c)
{ if (inb(x, y)) *PX(x, y) = c; }

static uint32_t blend(uint32_t dst, uint32_t src, int a)
{
    int ia = 255 - a;
    int r = ((int)((src >> 16) & 255) * a + (int)((dst >> 16) & 255) * ia) / 255;
    int g = ((int)((src >>  8) & 255) * a + (int)((dst >>  8) & 255) * ia) / 255;
    int b = ((int)( src        & 255) * a + (int)( dst        & 255) * ia) / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void d_px_a(int x, int y, uint32_t c, int a)
{
    if (!inb(x, y) || a <= 0) return;
    if (a >= 255) { *PX(x, y) = c; return; }
    *PX(x, y) = blend(*PX(x, y), c, a);
}

void d_rect(int x, int y, int w, int h, uint32_t c)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) d_px(x + i, y + j, c);
}

void d_rect_a(int x, int y, int w, int h, uint32_t c, int a)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) d_px_a(x + i, y + j, c, a);
}

void d_line(int x0, int y0, int x1, int y1, uint32_t c, int a)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int n = dx > dy ? dx : dy;
    if (n == 0) { d_px_a(x0, y0, c, a); return; }
    for (int i = 0; i <= n; i++)
        d_px_a(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, c, a);
}

/* градиентная линия: цвет плавно из c1 в c2 */
void d_line_grad(int x0, int y0, int x1, int y1, uint32_t c1, uint32_t c2, int a)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int n = dx > dy ? dx : dy;
    if (n == 0) { d_px_a(x0, y0, c1, a); return; }
    for (int i = 0; i <= n; i++) {
        int t = i * 255 / n;
        uint32_t c = blend(c1, c2, t);
        d_px_a(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, c, a);
    }
}

/* круг (заливка) */
void d_dot(int cx, int cy, int r, uint32_t c, int a)
{
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r) d_px_a(cx + i, cy + j, c, a);
}

static int isqrt_i(int n)
{ int r = 0; while ((r + 1) * (r + 1) <= n) r++; return r; }

/* отступ для строки dy скруглённого прямоугольника высоты h с радиусом r */
static int rr_inset(int dy, int h, int r)
{
    if (dy < r) { int t = r - dy; return r - isqrt_i(r * r - t * t); }
    if (dy > h - 1 - r) { int t = dy - (h - 1 - r); return r - isqrt_i(r * r - t * t); }
    return 0;
}

/* скруглённый прямоугольник с альфа-смешиванием (glass-панели) */
void d_round(int x, int y, int w, int h, int r, uint32_t c, int a)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int dy = 0; dy < h; dy++) {
        int in = rr_inset(dy, h, r);
        for (int i = x + in; i < x + w - in; i++) d_px_a(i, y + dy, c, a);
    }
}

/* скруглённая рамка 1px */
void d_round_border(int x, int y, int w, int h, int r, uint32_t c, int a)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int dy = 0; dy < h; dy++) {
        int in = rr_inset(dy, h, r);
        d_px_a(x + in, y + dy, c, a);
        d_px_a(x + w - 1 - in, y + dy, c, a);
    }
    int in0 = rr_inset(0, h, r);
    for (int i = x + in0; i <= x + w - 1 - in0; i++) {
        d_px_a(i, y, c, a);
        d_px_a(i, y + h - 1, c, a);
    }
}

/* мягкая тень под окном */
void d_shadow(int x, int y, int w, int h)
{
    d_rect_a(x + 3, y + 8, w, h, 0x000000, 46);
    d_rect_a(x + 6, y + 14, w, h, 0x000000, 30);
    d_rect_a(x + 10, y + 20, w, h, 0x000000, 16);
}

/* полоса-прогресс / RAM-бар */
void d_bar(int x, int y, int w, int h, int pct, uint32_t fg)
{
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    d_round(x, y, w, h, h / 2, 0xffffff, 30);
    int fw = w * pct / 100;
    if (fw > h) d_round(x, y, fw, h, h / 2, fg, 235);
}

/* радиальное свечение (aurora-обои) */
void d_glow(int cx, int cy, int rad, uint32_t c, int amax)
{
    int r2 = rad * rad;
    int x0 = cx - rad < 0 ? 0 : cx - rad;
    int y0 = cy - rad < 0 ? 0 : cy - rad;
    int x1 = cx + rad >= fb_w ? fb_w - 1 : cx + rad;
    int y1 = cy + rad >= fb_h ? fb_h - 1 : cy + rad;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            int t = (r2 - d2) * 255 / r2;
            d_px_a(x, y, c, amax * t * t / (255 * 255));
        }
}

/* ══════════ ОБОИ ══════════
   mode 0: "Aurora" (фиолет+циан)   mode 1: "Nebula" (тёплый)
   Рисовать ОДИН РАЗ при входе в desktop / при смене темы. */
void d_wallpaper(int mode)
{
    if (!fb) return;
    uint32_t top = mode == 1 ? 0x181022u : 0x0a0f1eu;
    uint32_t bot = mode == 1 ? 0x0b1632u : 0x1a1334u;
    for (int y = 0; y < fb_h; y++) {
        int t = y * 256 / (fb_h - 1);
        int r = ((top >> 16 & 255) * (256 - t) + (bot >> 16 & 255) * t) >> 8;
        int g = ((top >>  8 & 255) * (256 - t) + (bot >>  8 & 255) * t) >> 8;
        int b = ((top       & 255) * (256 - t) + (bot       & 255) * t) >> 8;
        uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        for (int x = 0; x < fb_w; x++) *PX(x, y) = c;
    }
    if (mode == 1) {
        d_glow(fb_w * 80 / 100, fb_h * 18 / 100, fb_h * 70 / 100, 0xf59e0b, 44);
        d_glow(fb_w * 15 / 100, fb_h * 85 / 100, fb_h * 80 / 100, 0x8b7bff, 46);
    } else {
        d_glow(fb_w * 18 / 100, fb_h * 20 / 100, fb_h * 70 / 100, K_ACC, 60);
        d_glow(fb_w * 85 / 100, fb_h * 82 / 100, fb_h * 80 / 100, K_ACC2, 46);
    }
    /* звёзды (детерминированный xorshift) */
    uint32_t s = 0x9e3779b9u;
    for (int i = 0; i < 140; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17; int x = (int)(s % (uint32_t)fb_w);
        s ^= s << 13; s ^= s >> 7; s ^= s << 17; int y = (int)(s % (uint32_t)fb_h);
        d_px_a(x, y, 0xffffff, 36 + (int)((s >> 8) % 80u));
    }
    /* тонкие скан-линии */
    for (int y = 0; y < fb_h; y += 3)
        for (int x = 0; x < fb_w; x++) {
            uint32_t p = *PX(x, y);
            *PX(x, y) = ((((p >> 16 & 255) * 242) >> 8) << 16)
                      | ((((p >>  8 & 255) * 242) >> 8) <<  8)
                      |   (((p        & 255) * 242) >>  8);
        }
}

/* ══════════ ЛОГОТИП (гексагон + K) ══════════ */
static const int HX[6] = { 0, 866, 866, 0, -866, -866 };
static const int HY[6] = { -1000, -500, 500, 1000, 500, -500 };

void d_logo(int cx, int cy, int s, int a)
{
    int px[6], py[6];
    for (int i = 0; i < 6; i++) {
        px[i] = cx + HX[i] * s / 1000;
        py[i] = cy + HY[i] * s / 1000;
    }
    for (int i = 0; i < 6; i++)
        d_line_grad(px[i], py[i], px[(i + 1) % 6], py[(i + 1) % 6], K_ACC, K_ACC2, a);
    /* буква K */
    d_line_grad(cx - s * 25 / 100, cy - s * 38 / 100, cx - s * 25 / 100, cy + s * 38 / 100, K_ACC, K_ACC2, a);
    d_line_grad(cx - s * 25 / 100, cy, cx + s * 28 / 100, cy - s * 36 / 100, K_ACC, K_ACC2, a);
    d_line_grad(cx - s * 25 / 100, cy, cx + s * 28 / 100, cy + s * 36 / 100, K_ACC, K_ACC2, a);
}

/* ══════════ ТЕКСТ (через ваш шрифт 8×8 из kf_fb.c) ══════════ */

/* таблица строк — весь текст UI, чтобы не носить строки через Kenga */
static const char* LBL[] = {
    "KENGAOS",                 /* 0  */
    "\xd0\x90\xd0\xb3\xd0\xb5\xd0\xbd\xd1\x82\xd1\x8b",       /* 1 Агенты */
    "\xd0\x9c\xd0\xbe\xd0\xb4\xd0\xb5\xd0\xbb\xd1\x8c",       /* 2 Модель */
    "\xd0\xa4\xd0\xb0\xd0\xb9\xd0\xbb\xd1\x8b",               /* 3 Файлы */
    "\xd0\xa1\xd0\xb8\xd1\x81\xd1\x82\xd0\xb5\xd0\xbc\xd0\xb0", /* 4 Система */
    "\xd0\xb6\xd0\xb8\xd0\xb2\xd0\xbe\xd0\xb9 \xd0\xbb\xd0\xbe\xd0\xb3 \xc2\xb7 ipc", /* 5 */
    "\xd0\xa1\xd0\xbf\xd1\x80\xd0\xbe\xd1\x81\xd0\xb8\xd1\x82\xd1\x8c \xd0\xb0\xd0\xb3\xd0\xb5\xd0\xbd\xd1\x82\xd0\xb0\xe2\x80\xa6", /* 6 */
    "\xd0\x9e\xd1\x82\xd0\xbf\xd1\x80\xd0\xb0\xd0\xb2\xd0\xb8\xd1\x82\xd1\x8c", /* 7 Отправить */
    "CPU",                     /* 8  */
    "RAM",                     /* 9  */
    "\xd0\xb0\xd0\xbf\xd1\x82\xd0\xb0\xd0\xb9\xd0\xbc",       /* 10 */
    "0.6 aurora",              /* 11 */
    "\xd1\x81\xd0\xb8\xd1\x81\xd1\x82\xd0\xb5\xd0\xbc\xd0\xb0 \xd0\xb3\xd0\xbe\xd1\x82\xd0\xbe\xd0\xb2\xd0\xb0", /* 12 */
    "\xd0\x97\xd0\x90\xd0\x93\xd0\xa0\xd0\xa3\xd0\x97\xd0\x9a\xd0\x90 \xd0\xaf\xd0\x94\xd0\xa0\xd0\x90", /* 13 */
    "\xd0\xb0\xd0\xb3\xd0\xb5\xd0\xbd\xd1\x82-\xd0\xbd\xd0\xb0\xd1\x82\xd0\xb8\xd0\xb2\xd0\xbd\xd0\xb0\xd1\x8f \xd0\xbe\xd1\x81", /* 14 */
    "\xd0\x90", "\xd0\x9c", "\xd0\xa4", "\xd0\xa1", /* 15..18 буквы иконок */
    "%",                       /* 19 */
    ":",                       /* 20 */
    "\xc2\xb7",                /* 21 · */
};

void d_label(int x, int y, int id, uint32_t c)
{
    if (id < 0 || id >= (int)(sizeof(LBL) / sizeof(LBL[0]))) return;
    fb_puts(x, y, LBL[id], c);
}

/* число с дополнением нулями (pad=2 → 07) */
void d_num(int x, int y, long v, int pad, uint32_t c)
{
    char b[24]; int n = 0;
    long t = v < 0 ? -v : v;
    if (t == 0) b[n++] = '0';
    while (t > 0) { b[n++] = (char)('0' + t % 10); t /= 10; }
    while (n < pad) b[n++] = '0';
    char b2[24];
    for (int i = 0; i < n; i++) b2[i] = b[n - 1 - i];
    b2[n] = 0;
    fb_puts(x, y, b2, c);
}

/* чип-таблетка: glass + подпись + число + % */
void d_chip(int x, int y, int label_id, long val, uint32_t val_c)
{
    d_round(x, y, 96, 20, 10, 0xffffff, 16);
    d_label(x + 8, y + 6, label_id, K_DIM);
    d_num(x + 42, y + 6, val, 0, val_c);
    d_label(x + 58, y + 6, 19, K_DIM);
}

/* Kenga freestanding intrinsic ABI.  The compiler prefixes intrinsic
   symbols with k_, while the design primitives above intentionally keep
   their short public names. */
int64_t k_d_px(int64_t x, int64_t y, int64_t c) { d_px(x, y, (uint32_t)c); return 0; }
int64_t k_d_px_a(int64_t x, int64_t y, int64_t c, int64_t a) { d_px_a(x, y, (uint32_t)c, (int)a); return 0; }
int64_t k_d_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t c) { d_rect(x, y, w, h, (uint32_t)c); return 0; }
int64_t k_d_rect_a(int64_t x, int64_t y, int64_t w, int64_t h, int64_t c, int64_t a) { d_rect_a(x, y, w, h, (uint32_t)c, a); return 0; }
int64_t k_d_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t c, int64_t a) { d_line(x0, y0, x1, y1, (uint32_t)c, a); return 0; }
int64_t k_d_line_grad(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t c1, int64_t c2, int64_t a) { d_line_grad(x0, y0, x1, y1, (uint32_t)c1, (uint32_t)c2, a); return 0; }
int64_t k_d_dot(int64_t x, int64_t y, int64_t r, int64_t c, int64_t a) { d_dot(x, y, r, (uint32_t)c, a); return 0; }
int64_t k_d_round(int64_t x, int64_t y, int64_t w, int64_t h, int64_t r, int64_t c, int64_t a) { d_round(x, y, w, h, r, (uint32_t)c, a); return 0; }
int64_t k_d_round_border(int64_t x, int64_t y, int64_t w, int64_t h, int64_t r, int64_t c, int64_t a) { d_round_border(x, y, w, h, r, (uint32_t)c, a); return 0; }
int64_t k_d_shadow(int64_t x, int64_t y, int64_t w, int64_t h) { d_shadow(x, y, w, h); return 0; }
int64_t k_d_bar(int64_t x, int64_t y, int64_t w, int64_t h, int64_t p, int64_t c) { d_bar(x, y, w, h, p, (uint32_t)c); return 0; }
int64_t k_d_glow(int64_t x, int64_t y, int64_t r, int64_t c, int64_t a) { d_glow(x, y, r, (uint32_t)c, a); return 0; }
int64_t k_d_wallpaper(int64_t mode) { d_wallpaper(mode); return 0; }
int64_t k_d_logo(int64_t x, int64_t y, int64_t s, int64_t a) { d_logo(x, y, s, a); return 0; }
int64_t k_d_label(int64_t x, int64_t y, int64_t id, int64_t c) { d_label(x, y, id, (uint32_t)c); return 0; }
int64_t k_d_num(int64_t x, int64_t y, int64_t v, int64_t pad, int64_t c) { d_num(x, y, (long)v, pad, (uint32_t)c); return 0; }
int64_t k_d_chip(int64_t x, int64_t y, int64_t id, int64_t v, int64_t c) { d_chip(x, y, id, (long)v, (uint32_t)c); return 0; }

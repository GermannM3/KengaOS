/* ═══════════════════════════════════════════════════════════════════════
   kernel/kf_design_v2.c — дизайн-система KengaOS 0.7 "Nebula"
   Графические примитивы для реального framebuffer ядра.
   Версия 2: deep-space wallpaper, orbital sphere, particle fields,
   nebula clouds, glassmorphism v2, reflections, bloom.
   ───────────────────────────────────────────────────────────────────────
   ИНТЕГРАЦИЯ:
   1) Положите файл в kernel/ как kf_design.c (или рядом, добавив в build)
   2) В scripts/build.sh добавьте к этапу компиляции C:
        $CC $CFLAGS -c kernel/kf_design.c -o build/kf_design.o
      и добавьте build/kf_design.o в команду линковки ld.lld
   3) СОГЛАСУЙТЕ 3 символа ниже с вашим kf_fb.c:
        fb, fb_w, fb_h, fb_pitch  — база и геометрия framebuffer
        fb_puts(x,y,s,fg)         — ваша функция вывода UTF-8 строки
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

/* ── палитра v2 "Nebula" — более глубокие, насыщенные тона ── */
#define K_BG      0x040814u   /* глубокий тёмно-синий фон      */
#define K_BG2     0x0a0e24u   /* вторичный фон                 */
#define K_GLASS   0x101830u   /* glass панель (тёмнее)         */
#define K_GLASS2  0x182040u   /* glass highlight               */
#define K_TEXT    0xf0f4ffu   /* основной текст (белый)        */
#define K_DIM     0x7a88b0u   /* приглушённый текст            */
#define K_ACC     0x9d7bffu   /* фиолетовый акцент (ярче)      */
#define K_ACC2    0x2dd4ffu   /* циан акцент (ярче)            */
#define K_OK      0x3ddc84u   /* зелёный OK                    */
#define K_WARN    0xffc93du   /* жёлтый warning                */
#define K_ERR     0xff5f5fu   /* красный error                 */
#define K_ORB1    0x6b4cbeu   /* orb фиолетовый                */
#define K_ORB2    0x2a9ec4u   /* orb циан                      */

/* ══════════ БАЗОВЫЕ ПРИМИТИВЫ (unchanged, core functions) ══════════ */

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

/* мягкий круг с градиентом альфы (для glow) */
void d_disc_soft(int cx, int cy, int r, uint32_t c, int amax)
{
    int r2 = r * r;
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++) {
            int d2 = i * i + j * j;
            if (d2 >= r2) continue;
            int t = (r2 - d2) * 255 / r2;
            d_px_a(cx + i, cy + j, c, amax * t * t / (255 * 255));
        }
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

/* скруглённый прямоугольник с верхней "glass" подсветкой (reflection) */
void d_round_glass(int x, int y, int w, int h, int r, uint32_t c, int a)
{
    d_round(x, y, w, h, r, c, a);
    /* верхняя полоска света — имитация отражения на стекле */
    int hl_h = h / 5;
    if (hl_h > 24) hl_h = 24;
    if (hl_h < 4) hl_h = 4;
    for (int dy = 0; dy < hl_h && dy < h; dy++) {
        int in = rr_inset(dy, h, r);
        int line_a = a * (hl_h - dy) * 30 / (hl_h * 100);
        if (line_a > 0)
            for (int i = x + in; i < x + w - in; i++)
                d_px_a(i, y + dy, 0xffffff, line_a);
    }
}

/* мягкая тень под окном (v2 — больше слоёв, мягче) */
void d_shadow(int x, int y, int w, int h)
{
    d_rect_a(x + 2, y + 4, w, h, 0x000000, 30);
    d_rect_a(x + 4, y + 8, w, h, 0x000000, 24);
    d_rect_a(x + 8, y + 14, w, h, 0x000000, 18);
    d_rect_a(x + 14, y + 22, w, h, 0x000000, 10);
}

/* полоса-прогресс / RAM-бар */
void d_bar(int x, int y, int w, int h, int pct, uint32_t fg)
{
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    d_round(x, y, w, h, h / 2, 0xffffff, 22);
    int fw = w * pct / 100;
    if (fw > h) d_round(x, y, fw, h, h / 2, fg, 220);
}

/* радиальное свечение (aurora-обои) v1 kept for compatibility */
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

/* ══════════ НОВЫЕ ПРИМИТИВЫ v2 ══════════ */

/* Небула — большое мягкое светящееся пятно (туманность) */
void d_nebula(int cx, int cy, int rx, int ry, uint32_t c, int amax)
{
    int rx2 = rx * rx;
    int ry2 = ry * ry;
    int x0 = cx - rx < 0 ? 0 : cx - rx;
    int y0 = cy - ry < 0 ? 0 : cy - ry;
    int x1 = cx + rx >= fb_w ? fb_w - 1 : cx + rx;
    int y1 = cy + ry >= fb_h ? fb_h - 1 : cy + ry;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx;
            int dy = y - cy;
            /* эллиптическое расстояние */
            int d2 = dx * dx * ry2 + dy * dy * rx2;
            if (d2 >= rx2 * ry2) continue;
            int t = (rx2 * ry2 - d2) * 255 / (rx2 * ry2);
            /* soften the falloff */
            int fa = amax * t * t / (255 * 255);
            d_px_a(x, y, c, fa / 2);
        }
}

/* Поле звёзд — детерминированный xorshift, N звёзд с вариациями яркости */
void d_stars(int count, uint32_t seed)
{
    uint32_t s = seed ? seed : 0x9e3779b9u;
    for (int i = 0; i < count; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        int x = (int)(s % (uint32_t)fb_w);
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        int y = (int)(s % (uint32_t)fb_h);
        int a = 20 + (int)((s >> 8) % 120u);
        int size = (s >> 4) % 3;  /* 0=1px, 1=1px bright, 2=small cross */
        if (size == 0) {
            d_px_a(x, y, 0xffffff, a);
        } else if (size == 1) {
            d_px_a(x, y, 0xffffff, a + 40);
            if (a > 100) {
                d_px_a(x - 1, y, 0xffffff, a / 4);
                d_px_a(x + 1, y, 0xffffff, a / 4);
                d_px_a(x, y - 1, 0xffffff, a / 4);
                d_px_a(x, y + 1, 0xffffff, a / 4);
            }
        } else {
            d_px_a(x, y, 0xc8d8ff, a);
        }
    }
}

/* Мелкие частицы (pixel dust) — много точек с низкой альфой */
void d_dust(int count, uint32_t seed)
{
    uint32_t s = seed ? seed : 0x6c078965u;
    for (int i = 0; i < count; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        int x = (int)(s % (uint32_t)fb_w);
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        int y = (int)(s % (uint32_t)fb_h);
        int a = 8 + (int)((s >> 8) % 30u);
        uint32_t c = ((s >> 12) % 2) ? 0x9d7bff : 0x2dd4ff;
        d_px_a(x, y, c, a);
    }
}

/* Виньетка — затемнение по краям */
void d_vignette(void)
{
    int cx = fb_w / 2;
    int cy = fb_h / 2;
    int max_r2 = cx * cx + cy * cy;
    for (int y = 0; y < fb_h; y += 2) {  /* skip every other line for speed */
        for (int x = 0; x < fb_w; x += 2) {
            int dx = x - cx;
            int dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 < max_r2 / 4) continue;  /* center stays bright */
            int t = (d2 - max_r2 / 4) * 255 / (max_r2 - max_r2 / 4);
            if (t > 80) t = 80;
            d_px_a(x, y, 0x000000, t);
            d_px_a(x + 1, y, 0x000000, t);
            d_px_a(x, y + 1, 0x000000, t);
            d_px_a(x + 1, y + 1, 0x000000, t);
        }
    }
}

/* Scanlines — горизонтальные линии для CRT-эффекта */
void d_scanlines(int spacing, int strength)
{
    for (int y = 0; y < fb_h; y += spacing)
        for (int x = 0; x < fb_w; x++) {
            uint32_t p = *PX(x, y);
            int f = 255 - strength;
            *PX(x, y) = ((((p >> 16 & 255) * f) >> 8) << 16)
                      | ((((p >>  8 & 255) * f) >> 8) <<  8)
                      |   (((p        & 255) * f) >>  8);
        }
}

/* ══════════ ОРБИТАЛЬНАЯ СФЕРА (как на primer.jpg) ══════════ */

/* Градиентный диск — основа сферы */
static void d_grad_disc(int cx, int cy, int r, uint32_t c1, uint32_t c2)
{
    int r2 = r * r;
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++) {
            int d2 = i * i + j * j;
            if (d2 >= r2) continue;
            int t = d2 * 255 / r2;
            uint32_t c = blend(c1, c2, t);
            d_px_a(cx + i, cy + j, c, 255);
        }
}

/* Орбитальное кольцо — тонкое эллиптическое кольцо частиц */
void d_orb_ring(int cx, int cy, int rx, int ry, uint32_t c, int a, int dots)
{
    for (int i = 0; i < dots; i++) {
        int angle = i * 360 / dots;
        /* эллипс с наклоном */
        int sx = rx * (int)(1000 * (angle % 90) / 90) / 1000;
        /* упрощённый: окружность */
        int x = cx + rx * (1000 - (angle % 180) * 11) / 1000;
        int y = cy + ry * (angle % 360 - 180) / 180;
        /* actually use trig-like via lookup would be better, simplified: */
        int px = cx + (rx * (i - dots / 2)) / (dots / 2);
        int py = cy + (ry * (i % 7 - 3)) / 4;
        if ((i * 7) % 3 == 0)  /* sparse dots */
            d_px_a(px, py, c, a);
    }
}

/* Основная орбитальная сфера — как на primer.jpg */
void d_orb(int cx, int cy, int r)
{
    /* внешнее свечение */
    d_disc_soft(cx, cy, r + 40, K_ORB1, 25);
    d_disc_soft(cx, cy, r + 25, K_ORB2, 18);
    /* основной градиентный диск: фиолетовый центр → циан край */
    d_grad_disc(cx, cy, r, K_ORB1, K_ORB2);
    /* внутренняя подсветка (блик) */
    d_disc_soft(cx - r / 3, cy - r / 3, r / 2, 0xffffff, 20);
    /* частицы вокруг */
    d_dust(60, (uint32_t)cx * 7 + (uint32_t)cy * 13);
}

/* ══════════ ОБОИ v2 "Deep Space" ══════════
   Рисовать ОДИН РАЗ при входе в desktop / при смене темы. */
void d_wallpaper(int mode)
{
    if (!fb) return;
    /* глубокий градиент фона */
    uint32_t top = 0x030510u;
    uint32_t bot = mode == 1 ? 0x120828u : 0x0a0e24u;
    for (int y = 0; y < fb_h; y++) {
        int t = y * 256 / (fb_h - 1);
        int r = ((top >> 16 & 255) * (256 - t) + (bot >> 16 & 255) * t) >> 8;
        int g = ((top >>  8 & 255) * (256 - t) + (bot >>  8 & 255) * t) >> 8;
        int b = ((top       & 255) * (256 - t) + (bot       & 255) * t) >> 8;
        uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        for (int x = 0; x < fb_w; x++) *PX(x, y) = c;
    }

    /* nebula clouds — большие туманности (как на primer.jpg) */
    if (mode == 1) {  /* warm nebula */
        d_nebula(fb_w * 75 / 100, fb_h * 15 / 100, fb_w * 45 / 100, fb_h * 35 / 100, 0x8b4a9e, 18);
        d_nebula(fb_w * 20 / 100, fb_h * 80 / 100, fb_w * 50 / 100, fb_h * 40 / 100, 0x4a3a8e, 16);
        d_glow(fb_w * 80 / 100, fb_h * 20 / 100, fb_h * 55 / 100, 0xf59e0b, 22);
    } else {  /* aurora (default) */
        d_nebula(fb_w * 22 / 100, fb_h * 18 / 100, fb_w * 40 / 100, fb_h * 30 / 100, 0x5a3aaa, 20);
        d_nebula(fb_w * 82 / 100, fb_h * 78 / 100, fb_w * 45 / 100, fb_h * 35 / 100, 0x2a6a9a, 18);
        d_glow(fb_w * 15 / 100, fb_h * 22 / 100, fb_h * 60 / 100, K_ACC, 45);
        d_glow(fb_w * 88 / 100, fb_h * 82 / 100, fb_h * 65 / 100, K_ACC2, 32);
    }

    /* дополнительные малые glow для глубины */
    d_glow(fb_w * 50 / 100, fb_h * 50 / 100, fb_h * 30 / 100, 0x1a1040, 14);

    /* звёзды: 800 (вместо 140) */
    d_stars(800, 0x9e3779b9u);

    /* pixel dust — мелкие частицы */
    d_dust(400, 0x6c078965u);

    /* виньетка */
    d_vignette();

    /* scanlines — лёгкие, для CRT-эффекта */
    d_scanlines(3, 18);
}

/* ══════════ ОТРАЖЕНИЕ (reflection внизу экрана) ══════════ */
void d_reflection(int y0, int h, int strength)
{
    for (int y = 0; y < h && y0 + y < fb_h; y++) {
        int src_y = y0 - y - 1;
        if (src_y < 0) continue;
        int a = strength * (h - y) / h;
        for (int x = 0; x < fb_w; x++) {
            uint32_t src = *PX(x, src_y);
            /* darken and desaturate */
            int r = (src >> 16) & 255;
            int g = (src >>  8) & 255;
            int b =  src        & 255;
            r = r * 40 / 100;
            g = g * 45 / 100;
            b = b * 55 / 100;
            uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            d_px_a(x, y0 + y, c, a);
        }
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
    "0.7 nebula",              /* 11 */
    "\xd1\x81\xd0\xb8\xd1\x81\xd1\x82\xd0\xb5\xd0\xbc\xd0\xb0 \xd0\xb3\xd0\xbe\xd1\x82\xd0\xbe\xd0\xb2\xd0\xb0", /* 12 */
    "\xd0\x97\xd0\x90\xd0\x93\xd0\xa0\xd0\xa3\xd0\x97\xd0\x9a\xd0\x90 \xd0\xaf\xd0\x94\xd0\xa0\xd0\x90", /* 13 */
    "\xd0\xb0\xd0\xb3\xd0\xb5\xd0\xbd\xd1\x82-\xd0\xbd\xd0\xb0\xd1\x82\xd0\xb8\xd0\xb2\xd0\xbd\xd0\xb0\xd1\x8f \xd0\xbe\xd1\x81", /* 14 */
    "\xd0\x90", "\xd0\x9c", "\xd0\xa4", "\xd0\xa1", /* 15..18 буквы иконок */
    "%",                       /* 19 */
    ":",                       /* 20 */
    "\xc2\xb7",                /* 21 · */
    "Kenga",                   /* 22 — для orb-сферы */
    "AGENTS",                  /* 23 */
    "MODEL",                   /* 24 */
    "FILES",                   /* 25 */
    "SYSTEM",                  /* 26 */
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
    d_round(x, y, 96, 20, 10, 0xffffff, 14);
    d_label(x + 8, y + 6, label_id, K_DIM);
    d_num(x + 42, y + 6, val, 0, val_c);
    d_label(x + 58, y + 6, 19, K_DIM);
}

/* ══════════ GLASS CARD v2 (как на primer.jpg) ══════════ */
/* Floating glass panel с светящейся рамкой и отражением */
void d_glass_card(int x, int y, int w, int h, int r, int focused)
{
    /* тень */
    d_shadow(x, y, w, h);
    /* тело glass */
    d_round_glass(x, y, w, h, r, K_GLASS, 200);
    /* верхняя бликовая полоска */
    d_round(x, y, w, r > 16 ? 16 : r, r > 8 ? 8 : r, 0xffffff, 10);
    /* рамка */
    d_round_border(x, y, w, h, r, 0xffffff, 18);
    if (focused) {
        /* светящаяся акцентная рамка */
        d_round_border(x - 1, y - 1, w + 2, h + 2, r + 1, K_ACC, 50);
        d_round_border(x - 2, y - 2, w + 4, h + 4, r + 2, K_ACC2, 25);
    }
}

/* ══════════ Kenga freestanding intrinsic ABI ══════════ */
int64_t k_d_px(int64_t x, int64_t y, int64_t c) { d_px(x, y, (uint32_t)c); return 0; }
int64_t k_d_px_a(int64_t x, int64_t y, int64_t c, int64_t a) { d_px_a(x, y, (uint32_t)c, (int)a); return 0; }
int64_t k_d_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t c) { d_rect(x, y, w, h, (uint32_t)c); return 0; }
int64_t k_d_rect_a(int64_t x, int64_t y, int64_t w, int64_t h, int64_t c, int64_t a) { d_rect_a(x, y, w, h, (uint32_t)c, a); return 0; }
int64_t k_d_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t c, int64_t a) { d_line(x0, y0, x1, y1, (uint32_t)c, a); return 0; }
int64_t k_d_line_grad(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t c1, int64_t c2, int64_t a) { d_line_grad(x0, y0, x1, y1, (uint32_t)c1, (uint32_t)c2, a); return 0; }
int64_t k_d_dot(int64_t x, int64_t y, int64_t r, int64_t c, int64_t a) { d_dot(x, y, r, (uint32_t)c, a); return 0; }
int64_t k_d_disc_soft(int64_t x, int64_t y, int64_t r, int64_t c, int64_t a) { d_disc_soft(x, y, r, (uint32_t)c, a); return 0; }
int64_t k_d_round(int64_t x, int64_t y, int64_t w, int64_t h, int64_t r, int64_t c, int64_t a) { d_round(x, y, w, h, r, (uint32_t)c, a); return 0; }
int64_t k_d_round_border(int64_t x, int64_t y, int64_t w, int64_t h, int64_t r, int64_t c, int64_t a) { d_round_border(x, y, w, h, r, (uint32_t)c, a); return 0; }
int64_t k_d_round_glass(int64_t x, int64_t y, int64_t w, int64_t h, int64_t r, int64_t c, int64_t a) { d_round_glass(x, y, w, h, r, (uint32_t)c, a); return 0; }
int64_t k_d_shadow(int64_t x, int64_t y, int64_t w, int64_t h) { d_shadow(x, y, w, h); return 0; }
int64_t k_d_bar(int64_t x, int64_t y, int64_t w, int64_t h, int64_t p, int64_t c) { d_bar(x, y, w, h, p, (uint32_t)c); return 0; }
int64_t k_d_glow(int64_t x, int64_t y, int64_t r, int64_t c, int64_t a) { d_glow(x, y, r, (uint32_t)c, a); return 0; }
int64_t k_d_nebula(int64_t x, int64_t y, int64_t rx, int64_t ry, int64_t c, int64_t a) { d_nebula(x, y, rx, ry, (uint32_t)c, a); return 0; }
int64_t k_d_stars(int64_t count, int64_t seed) { d_stars(count, (uint32_t)seed); return 0; }
int64_t k_d_dust(int64_t count, int64_t seed) { d_dust(count, (uint32_t)seed); return 0; }
int64_t k_d_vignette(void) { d_vignette(); return 0; }
int64_t k_d_scanlines(int64_t spacing, int64_t strength) { d_scanlines(spacing, strength); return 0; }
int64_t k_d_orb(int64_t x, int64_t y, int64_t r) { d_orb(x, y, r); return 0; }
int64_t k_d_reflection(int64_t y0, int64_t h, int64_t strength) { d_reflection(y0, h, strength); return 0; }
int64_t k_d_wallpaper(int64_t mode) { d_wallpaper(mode); return 0; }
int64_t k_d_logo(int64_t x, int64_t y, int64_t s, int64_t a) { d_logo(x, y, s, a); return 0; }
int64_t k_d_label(int64_t x, int64_t y, int64_t id, int64_t c) { d_label(x, y, id, (uint32_t)c); return 0; }
int64_t k_d_num(int64_t x, int64_t y, int64_t v, int64_t pad, int64_t c) { d_num(x, y, (long)v, pad, (uint32_t)c); return 0; }
int64_t k_d_chip(int64_t x, int64_t y, int64_t id, int64_t v, int64_t c) { d_chip(x, y, id, (long)v, (uint32_t)c); return 0; }
int64_t k_d_glass_card(int64_t x, int64_t y, int64_t w, int64_t h, int64_t r, int64_t focused) { d_glass_card(x, y, w, h, r, focused); return 0; }

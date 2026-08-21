/* kf_fb.c — KengaOS linear-framebuffer driver (Limine v12).
 *
 * Pure-pixel drawing primitives + a compact 8x8 bitmap font, exposed to the
 * Kenga kernel via FFI. Nothing here touches libc; it only writes to the
 * memory-mapped linear framebuffer that Limine provides.
 *
 * Kenga intrinsics (emit-c mangles a `fb_*` name to `k_fb_*`) map onto the
 * C functions below. If no framebuffer was handed out (fb_init gets 0), every
 * draw call becomes a no-op, so the kernel never crashes on headless boot.
 */

#include "kf_rt.h"
extern void k_design_fb_sync(uintptr_t pixels, int w, int h, int pitch);

/* --- global framebuffer state ------------------------------------------- */

static uintptr_t fb_addr = 0;
static uint32_t  fb_w = 0;
static uint32_t  fb_h = 0;
static uint32_t  fb_pitch = 0;
static uint8_t   fb_bpp = 0;
static int       fb_ok = 0;

/* Print an int64 to the UART (COM1, 0x3F8) as decimal. Used for boot/FB
   diagnostics. Avoids a static-buffer `str` return across the FFI boundary. */
static void uart_putc_ch(uint8_t c) {
    volatile uint8_t unused;
    __asm__ __volatile__("outb %0, %1" : : "a"(c), "Nd"((uint16_t)0x3F8));
    (void)unused;
}

int64_t k_kf_puti(int64_t n) {
    unsigned long long v;
    char tmp[24];
    int t = 0;
    if (n < 0) {
        uart_putc_ch((uint8_t)'-');
        v = (unsigned long long)(-(n + 1)) + 1ull;
    } else {
        v = (unsigned long long)n;
    }
    do { tmp[t++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (t) uart_putc_ch((uint8_t)tmp[--t]);
    return 0;
}

int64_t k_fb_init(int64_t fb) {
    fb_ok = 0;
    fb_addr = 0; fb_w = 0; fb_h = 0; fb_pitch = 0; fb_bpp = 0;
    if (!fb) return 0;

    /* struct limine_framebuffer (Limine v12):
         address* u64 @0, width u64 @8, height u64 @16, pitch u64 @24,
         bpp u16 @32, memory_model u8 @34, r/g/b mask sizes+shifts @35..40.
       Limine identity-maps low physical memory for the kernel, so the
       framebuffer's physical address is writable directly (this matches the
       proven kenga-os reference, which writes fb->address as-is). */
    fb_addr  = (uintptr_t)*(uint64_t*)fb;               /* +0  */
    fb_w     = (uint32_t)*(uint64_t*)(fb + 8);          /* +8  */
    fb_h     = (uint32_t)*(uint64_t*)(fb + 16);         /* +16 */
    fb_pitch = (uint32_t)*(uint64_t*)(fb + 24);         /* +24 */
    fb_bpp   = (uint8_t)*(uint16_t*)(fb + 32);          /* +32 */

    if (!fb_addr || fb_w == 0 || fb_h == 0 || (fb_bpp != 32 && fb_bpp != 24)) {
        fb_addr = 0; fb_w = 0; fb_h = 0; fb_pitch = 0; fb_bpp = 0;
        return 0;
    }
    fb_ok = 1;
    k_design_fb_sync(fb_addr, (int)fb_w, (int)fb_h, (int)(fb_pitch / 4));
    return 1;
}

int64_t k_fb_ready(void)  { return fb_ok; }
int64_t k_fb_width(void)  { return (int64_t)fb_w; }
int64_t k_fb_height(void) { return (int64_t)fb_h; }

/* --- double buffering ---------------------------------------------------
   The desktop draws a whole frame into fb_back (BSS), then k_fb_end_frame()
   blits it to the visible framebuffer in one pass. This removes the flicker
   of drawing many shapes directly onto the visible screen. */
static uint8_t fb_back[1280 * 800 * 4];
static uintptr_t fb_target = 0;   /* 0 = draw to fb_addr */

static uintptr_t fb_cur(void) { return fb_target ? fb_target : fb_addr; }

int64_t k_fb_begin_frame(void) {
    if (!fb_ok) return 0;
    fb_target = (uintptr_t)fb_back;
    k_design_fb_sync(fb_target, (int)fb_w, (int)fb_h, (int)(fb_pitch / 4));
    return 1;
}

int64_t k_fb_end_frame(void) {
    if (!fb_ok || !fb_target) return 0;
    uint64_t n = (uint64_t)fb_pitch * fb_h / 4;
    volatile uint32_t* d = (volatile uint32_t*)fb_addr;
    uint32_t* s = (uint32_t*)fb_target;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
    fb_target = 0;
    k_design_fb_sync(fb_addr, (int)fb_w, (int)fb_h, (int)(fb_pitch / 4));
    return 1;
}

static void fb_put(uint32_t x, uint32_t y, uint32_t c) {
    if (!fb_ok) return;
    if (x >= fb_w || y >= fb_h) return;
    uintptr_t off = (uintptr_t)y * fb_pitch + (uintptr_t)x * (fb_bpp / 8);
    volatile uint8_t* p = (volatile uint8_t*)(fb_cur() + off);
    if (fb_bpp == 32) {
        *(volatile uint32_t*)p = c;
    } else { /* 24bpp, little-endian BGRX */
        p[0] = (uint8_t)(c >> 16);
        p[1] = (uint8_t)(c >> 8);
        p[2] = (uint8_t)(c);
    }
}

int64_t k_fb_putpixel(int64_t x, int64_t y, int64_t color) {
    if ((uint64_t)x >= fb_w || (uint64_t)y >= fb_h) return 0;
    fb_put((uint32_t)x, (uint32_t)y, (uint32_t)color);
    return 1;
}

/* XOR a pixel: framebuffer[x,y] ^= color. Drawing the same shape twice with
   XOR erases it, which is how the cursor is moved without a backbuffer. */
int64_t k_fb_getpixel(int64_t x, int64_t y) {
    if (!fb_ok || (uint64_t)x >= fb_w || (uint64_t)y >= fb_h) return 0;
    uintptr_t off = (uintptr_t)y * fb_pitch + (uintptr_t)x * (fb_bpp / 8);
    if (fb_bpp == 32) return (int64_t)(*(volatile uint32_t*)(fb_cur() + off));
    return 0;
}

int64_t k_fb_xor(int64_t x, int64_t y, int64_t color) {
    if ((uint64_t)x >= fb_w || (uint64_t)y >= fb_h) return 0;
    uintptr_t off = (uintptr_t)y * fb_pitch + (uintptr_t)x * (fb_bpp / 8);
    volatile uint32_t* p = (volatile uint32_t*)(fb_cur() + off);
    *p ^= (uint32_t)color;
    return 1;
}

/* Draw an 8x14 arrow cursor at (x,y) in XOR mode. Calling it twice on the
   same position erases it (XOR is its own inverse). */
int64_t k_fb_cursor(int64_t x, int64_t y) {
    static const uint8_t CURSOR[14] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
        0xE0, 0xC0, 0xE0, 0xF0, 0xF0, 0xF8
    };
    const uint32_t WHITE = 0xFFFFFF;
    for (int r = 0; r < 14; r++) {
        uint8_t row = CURSOR[r];
        for (int bit = 0; bit < 8; bit++)
            if (row & (0x80 >> bit))
                k_fb_xor(x + bit, y + r, WHITE);
    }
    return 1;
}

int64_t k_fb_fill(int64_t color) {
    uint32_t c = (uint32_t)color;
    for (uint32_t y = 0; y < fb_h; y++)
        for (uint32_t x = 0; x < fb_w; x++)
            fb_put(x, y, c);
    return fb_ok;
}

/* Fast rect fill: direct 32-bit writes (no per-pixel helper call). */
void k_fb_fill_rect(int64_t x0, int64_t y0, int64_t w, int64_t h, int64_t color) {
    if (!fb_ok) return;
    if (fb_bpp != 32) { for (int64_t yy = y0; yy < y0 + h && yy < fb_h; yy++) for (int64_t xx = x0; xx < x0 + w && xx < fb_w; xx++) fb_put((uint32_t)xx, (uint32_t)yy, (uint32_t)color); return; }
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 >= fb_w || y0 >= fb_h) return;
    if (x0 + w > fb_w) w = fb_w - x0;
    if (y0 + h > fb_h) h = fb_h - y0;
    if (w <= 0 || h <= 0) return;
    uint32_t c = (uint32_t)color;
    uintptr_t base = fb_cur() + (uintptr_t)y0 * fb_pitch + (uintptr_t)x0 * 4;
    for (int64_t yy = 0; yy < h; yy++) {
        volatile uint32_t* row = (volatile uint32_t*)(base + (uintptr_t)yy * fb_pitch);
        for (int64_t xx = 0; xx < w; xx++) row[xx] = c;
    }
}

/* Outlined rectangle. */
int64_t k_fb_rect(int64_t x0, int64_t y0, int64_t w, int64_t h, int64_t color) {
    if (w < 1 || h < 1) return 0;
    uint32_t c = (uint32_t)color;
    uint32_t x1 = (uint32_t)(x0 + w - 1);
    uint32_t y1 = (uint32_t)(y0 + h - 1);
    for (uint32_t x = (uint32_t)x0; x <= x1; x++) { fb_put(x, (uint32_t)y0, c); fb_put(x, y1, c); }
    for (uint32_t y = (uint32_t)y0; y <= y1; y++) { fb_put((uint32_t)x0, y, c); fb_put(x1, y, c); }
    return 1;
}

/* Filled rectangle. */
int64_t k_fb_hrect(int64_t x0, int64_t y0, int64_t w, int64_t h, int64_t color) {
    if (w < 1 || h < 1) return 0;
    uint32_t c = (uint32_t)color;
    for (uint32_t y = (uint32_t)y0; y < (uint32_t)(y0 + h); y++)
        for (uint32_t x = (uint32_t)x0; x < (uint32_t)(x0 + w); x++)
            fb_put(x, y, c);
    return 1;
}

/* --- glassmorphism primitives (design port) ---------------------------- */

/* integer square root (Newton) for rounded-corner geometry */
static int64_t dsqrti(int64_t n) {
    if (n <= 0) return 0;
    int64_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

/* Radial glow: soft light falloff around (cx,cy), color c, radius rad,
   maximum alpha amax at the center. Used for the nebula wallpaper and
   focused-window halos. */
int64_t k_fb_glow(int64_t cx, int64_t cy, int64_t rad, int64_t color, int64_t amax) {
    if (!fb_ok || rad <= 0) return 0;
    if (amax < 0) amax = 0;
    if (amax > 255) amax = 255;
    int64_t r2 = rad * rad;
    int64_t x0 = cx - rad < 0 ? 0 : cx - rad;
    int64_t y0 = cy - rad < 0 ? 0 : cy - rad;
    int64_t x1 = cx + rad >= (int64_t)fb_w ? (int64_t)fb_w - 1 : cx + rad;
    int64_t y1 = cy + rad >= (int64_t)fb_h ? (int64_t)fb_h - 1 : cy + rad;
    uint32_t fg = (uint32_t)color;
    uint32_t fr = (fg >> 16) & 0xff, fgc = (fg >> 8) & 0xff, fbc = fg & 0xff;
    uintptr_t base = fb_cur();
    for (int64_t y = y0; y <= y1; y++) {
        volatile uint32_t* row = (volatile uint32_t*)(base + (uintptr_t)y * fb_pitch + (uintptr_t)x0 * 4);
        int64_t dy = y - cy;
        for (int64_t x = x0; x <= x1; x++, row++) {
            int64_t dx = x - cx;
            int64_t d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            /* quadratic falloff: t in 0..255 */
            int64_t t = (r2 - d2) * 255 / r2;
            int64_t a = amax * t * t / (255 * 255);
            if (a <= 0) continue;
            uint32_t bg = *row;
            uint32_t ia = 255 - (uint32_t)a;
            uint32_t r = (uint32_t)((fr * (uint32_t)a + ((bg >> 16) & 0xff) * ia) >> 8);
            uint32_t g = (uint32_t)((fgc * (uint32_t)a + ((bg >> 8) & 0xff) * ia) >> 8);
            uint32_t b = (uint32_t)((fbc * (uint32_t)a + (bg & 0xff) * ia) >> 8);
            *row = (r << 16) | (g << 8) | b;
        }
    }
    return 1;
}

/* Alpha-blend a solid color over the current framebuffer content.
   alpha: 0..255 (255 = fully opaque). Reads back the pixel, so it works
   on the back buffer too. */
int64_t k_fb_blend_rect(int64_t x0, int64_t y0, int64_t w, int64_t h,
                        int64_t color, int64_t alpha) {
    if (!fb_ok || w < 1 || h < 1) return 0;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > (int64_t)fb_w) w = fb_w - x0;
    if (y0 + h > (int64_t)fb_h) h = fb_h - y0;
    if (w <= 0 || h <= 0) return 0;
    uint32_t a = (uint32_t)alpha; if (a > 255) a = 255;
    uint32_t ia = 255 - a;
    uint32_t fg = (uint32_t)color;
    uint32_t fr = (fg >> 16) & 0xff, fg_ = (fg >> 8) & 0xff, fb_ = fg & 0xff;
    uintptr_t base = fb_cur();
    for (int64_t yy = 0; yy < h; yy++) {
        volatile uint32_t* row = (volatile uint32_t*)(base + (uintptr_t)(y0 + yy) * fb_pitch + (uintptr_t)x0 * 4);
        for (int64_t xx = 0; xx < w; xx++) {
            uint32_t bg = row[xx];
            uint32_t r = ((fr * a + ((bg >> 16) & 0xff) * ia) >> 8);
            uint32_t g = ((fg_ * a + ((bg >> 8) & 0xff) * ia) >> 8);
            uint32_t b = ((fb_ * a + (bg & 0xff) * ia) >> 8);
            row[xx] = (r << 16) | (g << 8) | b;
        }
    }
    return 1;
}

/* Rounded rectangle fill (radius r on all four corners). */

/* accessors for other translation units (wallpaper cache blit) */
uintptr_t k_fb_base_cur(void) { return fb_cur(); }
int64_t   k_fb_pitch_get(void) { return (int64_t)fb_pitch; }

static void fb_blend_px(uint32_t* row, int64_t xx, uint32_t fr, uint32_t fg, uint32_t fb,
                        uint32_t a) {
    uint32_t bg = row[xx];
    uint32_t ia = 255 - a;
    uint32_t r2 = (fr * a + ((bg >> 16) & 0xff) * ia) >> 8;
    uint32_t g2 = (fg * a + ((bg >> 8) & 0xff) * ia) >> 8;
    uint32_t b2 = (fb * a + (bg & 0xff) * ia) >> 8;
    row[xx] = (r2 << 16) | (g2 << 8) | b2;
}

/* AA filled disc (traffic lights, dock indicators) */
int64_t k_fb_disc(int64_t x, int64_t y, int64_t r, int64_t color) {
    if (r < 0 || x - r < 0 || y - r < 0 ||
        x + r > (int64_t)fb_w || y + r > (int64_t)fb_h) return 0;
    uint32_t fg = (uint32_t)color;
    uint32_t fr = (fg >> 16) & 0xff, fgc = (fg >> 8) & 0xff, fbc = fg & 0xff;
    static const int off[4][2] = { {1,1},{3,1},{1,3},{3,3} };
    uintptr_t base = fb_cur();
    for (int64_t yy = -r - 1; yy <= r; yy++) {
        uint32_t* row = (uint32_t*)(base + (uintptr_t)(y + yy) * fb_pitch + (uintptr_t)(x - r - 1) * 4);
        for (int64_t xx = -r - 1; xx <= r; xx++) {
            int cov = 0;
            for (int s = 0; s < 4; s++) {
                int64_t sx = xx * 4 + off[s][0], sy = yy * 4 + off[s][1];
                int64_t d = dsqrti(sx * sx + sy * sy) - r * 4;
                int64_t c = 2 - d;
                if (c < 0) c = 0; if (c > 4) c = 4;
                cov += (int)c;
            }
            if (cov == 0) continue;
            fb_blend_px(row, xx + r + 1, fr, fgc, fbc,
                        (uint32_t)(cov >= 16 ? 255 : cov << 4));
        }
    }
    return 1;
}

/* --- Anti-aliased rounded rects (SDF, 2x2 supersampled) ---
   The pixel-perfect 'blocky corner' killer. Coverage from a signed
   distance field evaluated at 4 subsample points per pixel. */

static int rrect_cov4(int64_t px, int64_t py,     /* pixel coords */
                      int64_t cx4, int64_t cy4,   /* center, quarter-px */
                      int64_t hw4, int64_t hh4, int64_t r4) {
    /* returns summed coverage 0..16 over 4 subsamples */
    static const int off[4][2] = { {1,1},{3,1},{1,3},{3,3} };
    int cov = 0;
    for (int s = 0; s < 4; s++) {
        int64_t sx4 = px * 4 + off[s][0];
        int64_t sy4 = py * 4 + off[s][1];
        int64_t dx = sx4 - cx4; if (dx < 0) dx = -dx;
        int64_t dy = sy4 - cy4; if (dy < 0) dy = -dy;
        int64_t qx = dx - (hw4 - r4);
        int64_t qy = dy - (hh4 - r4);
        int64_t ox = qx > 0 ? qx : 0;
        int64_t oy = qy > 0 ? qy : 0;
        int64_t outer = dsqrti(ox * ox + oy * oy);
        int64_t inner = (qx > qy ? qx : qy); if (inner > 0) inner = 0;
        int64_t sd = outer + inner - r4;
        int64_t c = 2 - sd;          /* 2 quarter-px = half pixel soft edge */
        if (c < 0) c = 0; if (c > 4) c = 4;
        cov += (int)c;
    }
    return cov;
}


int64_t k_fb_rrect(int64_t x0, int64_t y0, int64_t w, int64_t h,
                   int64_t r, int64_t color) {
    if (w < 1 || h < 1 || r < 0) return 0;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > (int64_t)fb_w) w = fb_w - x0;
    if (y0 + h > (int64_t)fb_h) h = fb_h - y0;
    if (w <= 0 || h <= 0) return 0;
    uint32_t c = (uint32_t)color;
    uint32_t fr = (c >> 16) & 0xff, fgc = (c >> 8) & 0xff, fbc = c & 0xff;
    int64_t rr = r; if (rr > w / 2) rr = w / 2; if (rr > h / 2) rr = h / 2;
    int64_t cx4 = (x0 * 2 + w) * 2, cy4 = (y0 * 2 + h) * 2;
    int64_t hw4 = w * 2, hh4 = h * 2, r4 = rr * 4;
    uintptr_t base = fb_cur();
    for (int64_t yy = 0; yy < h; yy++) {
        uint32_t* row = (uint32_t*)(base + (uintptr_t)(y0 + yy) * fb_pitch + (uintptr_t)x0 * 4);
        int64_t yedge = (yy < rr + 1 || yy >= h - rr - 1);
        for (int64_t xx = 0; xx < w; xx++) {
            if (!yedge && xx >= rr + 1 && xx < w - rr - 1) { row[xx] = c; continue; }
            int cov = rrect_cov4(x0 + xx, y0 + yy, cx4, cy4, hw4, hh4, r4);
            if (cov >= 16) { row[xx] = c; continue; }
            if (cov == 0) continue;
            fb_blend_px(row, xx, fr, fgc, fbc, (uint32_t)(cov >= 16 ? 255 : cov << 4));
        }
    }
    return 1;
}

/* Rounded glass panel: alpha-blend a rounded rect over the current content.
   This is THE glassmorphism primitive (reference: rgba(22,28,48,.78), r=14). */
int64_t k_fb_blend_rrect(int64_t x0, int64_t y0, int64_t w, int64_t h,
                         int64_t r, int64_t color, int64_t alpha) {
    if (!fb_ok || w < 1 || h < 1 || r < 0 || alpha <= 0) return 0;
    if (alpha > 255) alpha = 255;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > (int64_t)fb_w) w = fb_w - x0;
    if (y0 + h > (int64_t)fb_h) h = fb_h - y0;
    if (w <= 0 || h <= 0) return 0;
    uint32_t a = (uint32_t)alpha;
    uint32_t fg = (uint32_t)color;
    uint32_t fr = (fg >> 16) & 0xff, fgc = (fg >> 8) & 0xff, fbc = fg & 0xff;
    int64_t rr = r; if (rr > w / 2) rr = w / 2; if (rr > h / 2) rr = h / 2;
    int64_t cx4 = (x0 * 2 + w) * 2, cy4 = (y0 * 2 + h) * 2;
    int64_t hw4 = w * 2, hh4 = h * 2, r4 = rr * 4;
    uintptr_t base = fb_cur();
    for (int64_t yy = 0; yy < h; yy++) {
        uint32_t* row = (uint32_t*)(base + (uintptr_t)(y0 + yy) * fb_pitch + (uintptr_t)x0 * 4);
        int64_t yedge = (yy < rr + 1 || yy >= h - rr - 1);
        for (int64_t xx = 0; xx < w; xx++) {
            if (!yedge && xx >= rr + 1 && xx < w - rr - 1) {
                fb_blend_px(row, xx, fr, fgc, fbc, a);
                continue;
            }
            int cov = rrect_cov4(x0 + xx, y0 + yy, cx4, cy4, hw4, hh4, r4);
            if (cov == 0) continue;
            uint32_t ae = (a * (uint32_t)cov) >> 4;   /* 0..255 */
            fb_blend_px(row, xx, fr, fgc, fbc, ae);
        }
    }
    return 1;
}

int64_t k_fb_blend_rrect_top(int64_t x0, int64_t y0, int64_t w, int64_t h,
                             int64_t r, int64_t color, int64_t alpha) {
    /* rounded TOP corners only (window titlebars). The bottom edge is
       straight: coverage is computed against a rect extended r px down. */
    if (!fb_ok || w < 1 || h < 1 || r < 0 || alpha <= 0) return 0;
    if (alpha > 255) alpha = 255;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > (int64_t)fb_w) w = fb_w - x0;
    if (y0 + h > (int64_t)fb_h) h = fb_h - y0;
    if (w <= 0 || h <= 0) return 0;
    uint32_t a = (uint32_t)alpha;
    uint32_t fg = (uint32_t)color;
    uint32_t fr = (fg >> 16) & 0xff, fgc = (fg >> 8) & 0xff, fbc = fg & 0xff;
    int64_t rr = r; if (rr > w / 2) rr = w / 2; if (rr > h) rr = h;
    int64_t cx4 = (x0 * 2 + w) * 2, cy4 = (y0 * 2 + h + rr) * 2;
    int64_t hw4 = w * 2, hh4 = (h + rr) * 2, r4 = rr * 4;
    uintptr_t base = fb_cur();
    for (int64_t yy = 0; yy < h; yy++) {
        uint32_t* row = (uint32_t*)(base + (uintptr_t)(y0 + yy) * fb_pitch + (uintptr_t)x0 * 4);
        int64_t yedge = (yy < rr + 1);
        for (int64_t xx = 0; xx < w; xx++) {
            if (!yedge && xx >= rr + 1 && xx < w - rr - 1) {
                fb_blend_px(row, xx, fr, fgc, fbc, a);
                continue;
            }
            int cov = rrect_cov4(x0 + xx, y0 + yy, cx4, cy4, hw4, hh4, r4);
            if (cov == 0) continue;
            uint32_t ae = (a * (uint32_t)cov) >> 4;
            fb_blend_px(row, xx, fr, fgc, fbc, ae);
        }
    }
    return 1;
}

/* Glass gradient rrect: c0 at top -> c1 at bottom, alpha constant.
   Replicates the reference .glass: linear-gradient(160deg, rgba(22,28,48,.78),
   rgba(10,14,26,.72)). */
int64_t k_fb_blend_rrect_grad(int64_t x0, int64_t y0, int64_t w, int64_t h,
                              int64_t r, int64_t c0, int64_t c1, int64_t alpha) {
    if (!fb_ok || w < 1 || h < 1 || r < 0 || alpha <= 0) return 0;
    if (alpha > 255) alpha = 255;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > (int64_t)fb_w) w = fb_w - x0;
    if (y0 + h > (int64_t)fb_h) h = fb_h - y0;
    if (w <= 0 || h <= 0) return 0;
    uint32_t a = (uint32_t)alpha;
    int64_t r0 = (c0 >> 16) & 0xff, g0 = (c0 >> 8) & 0xff, b0 = c0 & 0xff;
    int64_t r1 = (c1 >> 16) & 0xff, g1 = (c1 >> 8) & 0xff, b1 = c1 & 0xff;
    int64_t rr = r; if (rr > w / 2) rr = w / 2; if (rr > h / 2) rr = h / 2;
    int64_t cx4 = (x0 * 2 + w) * 2, cy4 = (y0 * 2 + h) * 2;
    int64_t hw4 = w * 2, hh4 = h * 2, r4 = rr * 4;
    uintptr_t base = fb_cur();
    for (int64_t yy = 0; yy < h; yy++) {
        uint32_t* row = (uint32_t*)(base + (uintptr_t)(y0 + yy) * fb_pitch + (uintptr_t)x0 * 4);
        /* color for this row: lerp with slight horizontal tilt (160deg) */
        int64_t t = yy * 255 / (h > 1 ? h - 1 : 1);
        uint32_t fr = (uint32_t)(r0 + (r1 - r0) * t / 255);
        uint32_t fg = (uint32_t)(g0 + (g1 - g0) * t / 255);
        uint32_t fb = (uint32_t)(b0 + (b1 - b0) * t / 255);
        int64_t yedge = (yy < rr + 1 || yy >= h - rr - 1);
        for (int64_t xx = 0; xx < w; xx++) {
            if (!yedge && xx >= rr + 1 && xx < w - rr - 1) {
                fb_blend_px(row, xx, fr, fg, fb, a);
                continue;
            }
            int cov = rrect_cov4(x0 + xx, y0 + yy, cx4, cy4, hw4, hh4, r4);
            if (cov == 0) continue;
            uint32_t ae = (a * (uint32_t)cov) >> 4;
            fb_blend_px(row, xx, fr, fg, fb, ae);
        }
    }
    return 1;
}

/* --- Vector icons (SDF union, anti-aliased, 20x20 box) ---
   type 0=agents 1=model 2=files 3=system. All geometry in quarter-px. */
static int64_t sd_circle(int64_t px4, int64_t py4, int64_t cx4, int64_t cy4, int64_t r4) {
    int64_t dx = px4 - cx4, dy = py4 - cy4;
    return dsqrti(dx * dx + dy * dy) - r4;
}
static int64_t sd_rrect(int64_t px4, int64_t py4, int64_t cx4, int64_t cy4,
                        int64_t hw4, int64_t hh4, int64_t r4) {
    int64_t dx = px4 - cx4; if (dx < 0) dx = -dx;
    int64_t dy = py4 - cy4; if (dy < 0) dy = -dy;
    int64_t qx = dx - (hw4 - r4), qy = dy - (hh4 - r4);
    int64_t ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    int64_t outer = dsqrti(ox * ox + oy * oy);
    int64_t inner = (qx > qy ? qx : qy); if (inner > 0) inner = 0;
    return outer + inner - r4;
}
static int64_t sd_capsule(int64_t px4, int64_t py4,
                          int64_t ax4, int64_t ay4, int64_t bx4, int64_t by4, int64_t r4) {
    int64_t abx = bx4 - ax4, aby = by4 - ay4;
    int64_t apx = px4 - ax4, apy = py4 - ay4;
    int64_t ab2 = abx * abx + aby * aby;
    int64_t t = 0;
    if (ab2 > 0) { t = (apx * abx + apy * aby); if (t < 0) t = 0; if (t > ab2) t = ab2; t = t / ab2; }
    int64_t cx = ax4 + abx * t, cy = ay4 + aby * t;
    int64_t dx = px4 - cx, dy = py4 - cy;
    return dsqrti(dx * dx + dy * dy) - r4;
}

static int64_t icon_sd(int type, int64_t px4, int64_t py4) {
    /* 1:1 SDF replicas of the reference SVG icons (24x24 viewBox, stroke 1.8).
       Grid: 20x20 px = 80x80 quarter-px. Stroke ~6 quarter-px (1.5px). */
    int64_t sd = 0x7fffffff;
    int64_t t;
    if (type == 0) {          /* agents: two people (reference paths) */
        /* head 1: circle cx9 cy8 r3.2 -> (30,27) r11 */
        t = sd_circle(px4, py4, 30, 27, 11); if (t < 0) t = -t; t -= 6;
        if (t < sd) sd = t;
        /* body 1: arc M3.5 19 c.6-3.2... -> upper arc of circle (30,72) r18 */
        if (py4 <= 78) {
            t = sd_circle(px4, py4, 30, 72, 18); if (t < 0) t = -t; t -= 6;
            if (t < sd) sd = t;
        }
        /* head 2: circle cx17 cy9 r2.6 -> (57,30) r9 */
        t = sd_circle(px4, py4, 57, 30, 9); if (t < 0) t = -t; t -= 6;
        if (t < sd) sd = t;
        /* body 2: arc M15.5 14.4 c2.3... -> upper arc (57,68) r15 */
        if (py4 <= 74) {
            t = sd_circle(px4, py4, 57, 68, 15); if (t < 0) t = -t; t -= 6;
            if (t < sd) sd = t;
        }
    } else if (type == 1) {   /* model: monitor pulse line (heartbeat) */
        /* M3 12 h4 l3 -7 l4 14 l3 -7 h4 -> pts (10,40)(23,40)(33,17)(47,63)(57,40)(70,40) */
        t = sd_capsule(px4, py4, 10, 40, 23, 40, 5); if (t < sd) sd = t;
        t = sd_capsule(px4, py4, 23, 40, 33, 17, 5); if (t < sd) sd = t;
        t = sd_capsule(px4, py4, 33, 17, 47, 63, 5); if (t < sd) sd = t;
        t = sd_capsule(px4, py4, 47, 63, 57, 40, 5); if (t < sd) sd = t;
        t = sd_capsule(px4, py4, 57, 40, 70, 40, 5); if (t < sd) sd = t;
    } else if (type == 2) {   /* files: folder (rounded rect + tab) */
        /* outline: ring of rrect c(40,44) hw26 hh19 r7 */
        t = sd_rrect(px4, py4, 40, 44, 26, 19, 7); if (t < 0) t = -t; t -= 6;
        if (t < sd) sd = t;
        /* tab: small capsule on the top edge, left (x 12..30 at y 21) */
        t = sd_capsule(px4, py4, 13, 21, 28, 21, 4); if (t < sd) sd = t;
    } else if (type == 3) {   /* system: settings gear */
        /* center ring r3.2*3.33=11 */
        t = sd_circle(px4, py4, 40, 40, 11); if (t < 0) t = -t; t -= 6;
        if (t < sd) sd = t;
        /* 8 spokes (r=16..26 quarter), N NE E SE S SW W NW */
        static const int8_t sp[8][4] = {
            { 0,-24,  0,-15}, { 17,-17, 11,-11}, { 24,0, 15,0}, { 17,17, 11,11},
            { 0, 24,  0, 15}, {-17,17, -11,11}, {-24,0, -15,0}, {-17,-17, -11,-11}
        };
        for (int i = 0; i < 8; i++) {
            t = sd_capsule(px4, py4, 40 + sp[i][0], 40 + sp[i][1],
                           40 + sp[i][2], 40 + sp[i][3], 4);
            if (t < sd) sd = t;
        }
    } else {                  /* terminal: rrect frame + >_ */
        t = sd_rrect(px4, py4, 40, 40, 27, 21, 8); if (t < 0) t = -t; t -= 6;
        if (t < sd) sd = t;
        t = sd_capsule(px4, py4, 23, 28, 34, 40, 5); if (t < sd) sd = t;
        t = sd_capsule(px4, py4, 34, 40, 23, 52, 5); if (t < sd) sd = t;
        t = sd_capsule(px4, py4, 43, 50, 57, 50, 5); if (t < sd) sd = t;
    }
    return sd;
}

int64_t k_fb_icon(int64_t x, int64_t y, int64_t type, int64_t color) {
    if (type < 0 || type > 4) return 0;
    if (x < 0 || y < 0 || x + 20 > (int64_t)fb_w || y + 20 > (int64_t)fb_h) return 0;
    uint32_t fg = (uint32_t)color;
    uint32_t fr = (fg >> 16) & 0xff, fgc = (fg >> 8) & 0xff, fbc = fg & 0xff;
    static const int off[4][2] = { {1,1},{3,1},{1,3},{3,3} };
    uintptr_t base = fb_cur();
    for (int64_t yy = 0; yy < 20; yy++) {
        uint32_t* row = (uint32_t*)(base + (uintptr_t)(y + yy) * fb_pitch + (uintptr_t)x * 4);
        for (int64_t xx = 0; xx < 20; xx++) {
            int cov = 0;
            for (int s = 0; s < 4; s++) {
                int64_t sd = icon_sd((int)type, xx * 4 + off[s][0], yy * 4 + off[s][1]);
                int64_t c = 2 - sd;
                if (c < 0) c = 0; if (c > 4) c = 4;
                cov += (int)c;
            }
            if (cov == 0) continue;
            fb_blend_px(row, xx, fr, fgc, fbc, (uint32_t)(cov >= 16 ? 255 : cov << 4));
        }
    }
    return 1;
}


/* Vertical linear gradient fill from c0 (top) to c1 (bottom). */
int64_t k_fb_grad_rect(int64_t x0, int64_t y0, int64_t w, int64_t h,
                       int64_t c0, int64_t c1) {
    if (w < 1 || h < 1) return 0;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > (int64_t)fb_w) w = fb_w - x0;
    if (y0 + h > (int64_t)fb_h) h = fb_h - y0;
    if (w <= 0 || h <= 0) return 0;
    uint32_t a = (uint32_t)c0, b = (uint32_t)c1;
    uintptr_t base = fb_cur();
    for (int64_t yy = 0; yy < h; yy++) {
        uint32_t t = (uint32_t)(yy * 255 / (h > 1 ? h - 1 : 1));
        uint32_t it = 255 - t;
        uint32_t c = ((((a >> 16) & 0xff) * it + ((b >> 16) & 0xff) * t) >> 8) << 16 |
                     ((((a >> 8) & 0xff) * it + ((b >> 8) & 0xff) * t) >> 8) << 8 |
                     (((a & 0xff) * it + (b & 0xff) * t) >> 8);
        volatile uint32_t* row = (volatile uint32_t*)(base + (uintptr_t)(y0 + yy) * fb_pitch + (uintptr_t)x0 * 4);
        for (int64_t xx = 0; xx < w; xx++) row[xx] = c;
    }
    return 1;
}

/* --- 8x8 bitmap font (ASCII 32..126) --------------------------------------
   Public domain (Daniel Hepper, font8x8_basic). MSB = leftmost pixel.      */
static const uint8_t FONT8X8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* ! */
    {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, /* # */
    {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00}, /* $ */
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}, /* % */
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, /* & */
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, /* ( */
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, /* ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* * */
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, /* , */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* . */
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, /* / */
    {0x7C,0xCE,0xD6,0xD6,0xE6,0xC6,0x7C,0x00}, /* 0 */
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, /* 1 */
    {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00}, /* 2 */
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, /* 3 */
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, /* 4 */
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, /* 5 */
    {0x3C,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, /* 6 */
    {0xFE,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, /* 7 */
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, /* 8 */
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, /* 9 */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, /* : */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, /* ; */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* < */
    {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00}, /* = */
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, /* > */
    {0x7C,0xC6,0x06,0x1C,0x18,0x00,0x18,0x00}, /* ? */
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00}, /* @ */
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, /* A */
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, /* B */
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, /* C */
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, /* D */
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, /* E */
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, /* F */
    {0x3C,0x66,0xC0,0xCE,0xC6,0x66,0x3E,0x00}, /* G */
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, /* H */
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* I */
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, /* J */
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, /* K */
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, /* L */
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}, /* M */
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, /* N */
    {0x38,0x6C,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, /* O */
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, /* P */
    {0x7C,0xC6,0xC6,0xC6,0xD6,0x7C,0x0E,0x00}, /* Q */
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, /* R */
    {0x7C,0xC6,0xE0,0x7C,0x0E,0xC6,0x7C,0x00}, /* S */
    {0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0x00}, /* T */
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, /* U */
    {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, /* V */
    {0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x00}, /* W */
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, /* X */
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, /* Y */
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00}, /* Z */
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, /* [ */
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, /* \ */
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, /* ] */
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* _ */
    {0x18,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, /* a */
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00}, /* b */
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}, /* c */
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00}, /* d */
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, /* e */
    {0x3C,0x66,0x60,0xF8,0x60,0x60,0xF0,0x00}, /* f */
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, /* g */
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, /* h */
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, /* i */
    {0x0C,0x00,0x1C,0x0C,0x0C,0xCC,0xCC,0x78}, /* j */
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, /* k */
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* l */
    {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00}, /* m */
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, /* n */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, /* o */
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, /* p */
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, /* q */
    {0x00,0x00,0xDC,0x76,0x66,0x60,0xF0,0x00}, /* r */
    {0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00}, /* s */
    {0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0x00}, /* t */
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, /* u */
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, /* v */
    {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00}, /* w */
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, /* x */
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC}, /* y */
    {0x00,0x00,0x7E,0x4C,0x18,0x32,0x7E,0x00}, /* z */
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, /* { */
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, /* | */
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, /* } */
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}  /* ~ */
};

/* Cyrillic 8x8 glyphs: index 0..63 = АБВ...Яабв...я (U+0410..U+044F) */

static const uint8_t FONT_CYR[64][8] = {

    {0x00,0x7e,0x11,0x11,0x11,0x7e,0x00,0x00},
    {0x00,0x7f,0x49,0x49,0x49,0x33,0x00,0x00},
    {0x00,0x7f,0x49,0x49,0x49,0x36,0x00,0x00},
    {0x00,0x7f,0x01,0x01,0x01,0x01,0x00,0x00},
    {0x00,0x3c,0x24,0x24,0x7f,0x40,0x40,0x00},
    {0x00,0x7f,0x49,0x49,0x49,0x49,0x00,0x00},
    {0x00,0x63,0x14,0x08,0x14,0x63,0x00,0x00},
    {0x00,0x36,0x49,0x49,0x49,0x36,0x00,0x00},
    {0x00,0x7f,0x40,0x7f,0x01,0x7f,0x00,0x00},
    {0x00,0x6b,0x55,0x40,0x7f,0x01,0x7f,0x00},
    {0x00,0x7f,0x08,0x14,0x22,0x41,0x00,0x00},
    {0x00,0x3e,0x02,0x02,0x3f,0x40,0x40,0x00},
    {0x00,0x7f,0x08,0x08,0x08,0x7f,0x00,0x00},
    {0x00,0x7f,0x08,0x08,0x08,0x7f,0x00,0x00},
    {0x00,0x3e,0x41,0x41,0x41,0x3e,0x00,0x00},
    {0x00,0x7f,0x01,0x01,0x01,0x7f,0x00,0x00},
    {0x00,0x7f,0x09,0x09,0x09,0x06,0x00,0x00},
    {0x00,0x3e,0x41,0x41,0x41,0x22,0x00,0x00},
    {0x00,0x01,0x01,0x7f,0x01,0x01,0x00,0x00},
    {0x00,0x07,0x08,0x48,0x08,0x7f,0x00,0x00},
    {0x00,0x08,0x3e,0x49,0x3e,0x08,0x00,0x00},
    {0x00,0x41,0x22,0x1c,0x22,0x41,0x00,0x00},
    {0x00,0x3f,0x20,0x20,0x20,0x7f,0x60,0x00},
    {0x00,0x07,0x08,0x08,0x08,0x7f,0x00,0x00},
    {0x00,0x7f,0x40,0x7f,0x40,0x7f,0x00,0x00},
    {0x00,0x7f,0x40,0x7f,0x40,0x7f,0x40,0x00},
    {0x00,0x03,0x01,0x7f,0x01,0x01,0x00,0x00},
    {0x00,0x43,0x48,0x48,0x48,0x7f,0x00,0x00},
    {0x00,0x7f,0x08,0x08,0x08,0x07,0x00,0x00},
    {0x00,0x22,0x41,0x49,0x49,0x3e,0x00,0x00},
    {0x00,0x51,0x29,0x09,0x29,0x51,0x00,0x00},
    {0x00,0x3e,0x21,0x21,0x3e,0x60,0x20,0x00},
    {0x00,0x00,0x3c,0x42,0x42,0x3e,0x00,0x00},
    {0x00,0x3c,0x42,0x42,0x3c,0x02,0x00,0x00},
    {0x00,0x00,0x7e,0x12,0x12,0x0c,0x00,0x00},
    {0x00,0x00,0x7e,0x02,0x02,0x02,0x00,0x00},
    {0x00,0x00,0x3c,0x24,0x7e,0x40,0x00,0x00},
    {0x00,0x00,0x3c,0x4a,0x4a,0x34,0x00,0x00},
    {0x00,0x00,0x66,0x18,0x66,0x00,0x00,0x00},
    {0x00,0x00,0x34,0x4a,0x4a,0x34,0x00,0x00},
    {0x00,0x00,0x7e,0x40,0x7e,0x40,0x7e,0x00},
    {0x00,0x6a,0x54,0x40,0x7e,0x40,0x7e,0x00},
    {0x00,0x00,0x7e,0x08,0x14,0x22,0x00,0x00},
    {0x00,0x00,0x3e,0x02,0x3e,0x40,0x00,0x00},
    {0x00,0x00,0x7e,0x08,0x08,0x7e,0x00,0x00},
    {0x00,0x00,0x7e,0x08,0x08,0x7e,0x00,0x00},
    {0x00,0x00,0x3c,0x42,0x42,0x3c,0x00,0x00},
    {0x00,0x00,0x7e,0x02,0x02,0x7e,0x00,0x00},
    {0x00,0x00,0x7e,0x12,0x12,0x0c,0x00,0x00},
    {0x00,0x00,0x3c,0x42,0x42,0x24,0x00,0x00},
    {0x00,0x00,0x02,0x7e,0x02,0x02,0x00,0x00},
    {0x00,0x00,0x06,0x28,0x28,0x3e,0x00,0x00},
    {0x00,0x08,0x3c,0x4a,0x3c,0x08,0x00,0x00},
    {0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x00},
    {0x00,0x00,0x3e,0x20,0x3e,0x60,0x00,0x00},
    {0x00,0x00,0x06,0x08,0x08,0x7e,0x00,0x00},
    {0x00,0x00,0x7e,0x40,0x7e,0x40,0x7e,0x00},
    {0x00,0x00,0x7e,0x40,0x7e,0x40,0x7e,0x40},
    {0x00,0x06,0x02,0x7e,0x02,0x02,0x00,0x00},
    {0x00,0x00,0x46,0x48,0x48,0x7e,0x00,0x00},
    {0x00,0x00,0x7e,0x10,0x10,0x0e,0x00,0x00},
    {0x00,0x00,0x24,0x4a,0x4a,0x3c,0x00,0x00},
    {0x00,0x00,0x52,0x2a,0x2a,0x52,0x00,0x00},
    {0x00,0x00,0x3c,0x22,0x3c,0x60,0x20,0x00},
};

int64_t k_fb_draw_char(int64_t x, int64_t y, int64_t fg, int64_t bg, int64_t c) {
    if ((uint64_t)x >= fb_w || (uint64_t)y >= fb_h) return 0;
    uint32_t f = (uint32_t)fg;
    uint32_t b = (uint32_t)bg;
    const uint8_t* glyph = 0;
    if (c >= 32 && c <= 126) {
        glyph = FONT8X8[(int)c - 32];
    } else if (c >= 0x410 && c <= 0x44F) {
        /* Cyrillic: U+0410..U+044F -> FONT_CYR index 0..63 */
        int cyr_idx = (int)(c - 0x410);
        if (cyr_idx >= 0 && cyr_idx < 64) glyph = FONT_CYR[cyr_idx];
    }
    if (!glyph) return 0;
    for (int r = 0; r < 8; r++) {
        uint8_t row = glyph[r];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t px = (row & (0x80 >> bit)) ? f : b;
            fb_put((uint32_t)x + bit, (uint32_t)y + r, px);
        }
    }
    return 8;
}

int64_t k_fb_text(int64_t x, int64_t y, int64_t fg, int64_t bg, const char* s) {
    if (!s) return 0;
    int64_t cx = x;
    for (; *s; s++) {
        if (*s == '\n') { cx = x; y += 10; continue; }
        k_fb_draw_char(cx, y, fg, bg, (int64_t)(unsigned char)*s);
        cx += 8;
    }
    return 1;
}

/* Smooth 2x text: renders the 8x8 font scaled to 16x16 with edge
   anti-aliasing (each glyph pixel -> 2x2 block; edge pixels get alpha
   blending). This is what kills the 'DOS look' in the UI. */
int64_t k_fb_draw_char2x(int64_t x, int64_t y, int64_t fg, int64_t bg, int64_t c) {
    if ((uint64_t)x >= fb_w - 15 || (uint64_t)y >= fb_h - 15) return 0;
    uint32_t f = (uint32_t)fg;
    uint32_t b = (uint32_t)bg;
    const uint8_t* glyph = 0;
    if (c >= 32 && c <= 126) {
        glyph = FONT8X8[(int)c - 32];
    } else if (c >= 0x410 && c <= 0x44F) {
        int cyr_idx = (int)(c - 0x410);
        if (cyr_idx >= 0 && cyr_idx < 64) glyph = FONT_CYR[cyr_idx];
    }
    if (!glyph) return 0;
    /* 16x16 coverage: each source pixel contributes 4 output pixels.
       Horizontal edges get 40% coverage on the right column, vertical
       edges 40% on the bottom row -> faux anti-aliasing. */
    for (int r = 0; r < 8; r++) {
        uint8_t row = glyph[r];
        uint8_t row_below = (r + 1 < 8) ? glyph[r + 1] : 0;
        for (int bit = 0; bit < 8; bit++) {
            int on = (row >> (7 - bit)) & 1;
            if (!on) continue;
            int on_right  = (bit + 1 < 8) ? (row >> (7 - (bit + 1))) & 1 : 0;
            int on_below  = (row_below >> (7 - bit)) & 1;
            int x2 = (int)x + bit * 2;
            int y2 = (int)y + r * 2;
            /* solid core */
            fb_put((uint32_t)x2, (uint32_t)y2, f);
            fb_put((uint32_t)x2 + 1, (uint32_t)y2, f);
            fb_put((uint32_t)x2, (uint32_t)y2 + 1, f);
            /* right edge: half coverage if the neighbor is off */
            if (!on_right) {
                uint32_t mix_r = ((f >> 16 & 255) * 128 + (b >> 16 & 255) * 127) >> 8;
                uint32_t mix_g = ((f >> 8 & 255) * 128 + (b >> 8 & 255) * 127) >> 8;
                uint32_t mix_b = ((f & 255) * 128 + (b & 255) * 127) >> 8;
                fb_put((uint32_t)x2 + 1, (uint32_t)y2, (mix_r << 16) | (mix_g << 8) | mix_b);
            } else {
                fb_put((uint32_t)x2 + 1, (uint32_t)y2, f);
            }
            /* bottom edge */
            if (!on_below) {
                uint32_t mix_r = ((f >> 16 & 255) * 128 + (b >> 16 & 255) * 127) >> 8;
                uint32_t mix_g = ((f >> 8 & 255) * 128 + (b >> 8 & 255) * 127) >> 8;
                uint32_t mix_b = ((f & 255) * 128 + (b & 255) * 127) >> 8;
                fb_put((uint32_t)x2 + 1, (uint32_t)y2 + 1, (mix_r << 16) | (mix_g << 8) | mix_b);
            } else {
                fb_put((uint32_t)x2 + 1, (uint32_t)y2 + 1, f);
            }
        }
    }
    /* clear the background for glyph spacing */
    return 16;
}

int64_t k_fb_text2x(int64_t x, int64_t y, int64_t fg, int64_t bg, const char* s) {
    int64_t cx = x;
    for (const char* p = s; *p; p++) {
        int64_t adv = k_fb_draw_char2x(cx, y, fg, bg, (int64_t)(unsigned char)*p);
        if (adv == 0) adv = 16;
        cx += adv;
    }
    return cx - x;
}

/* --- text console (M2.3): char buffer + cursor + scroll --- */
#define CON_COLS_MAX 160
#define CON_ROWS_MAX 90
#define CON_FG 0x00d4aa
#define CON_BG 0x0a0e14
#define CON_DIM 0x8892a4

static int con_cols = 0, con_rows = 0;
static int con_cx = 0, con_cy = 0;
static int con_buf[CON_COLS_MAX * CON_ROWS_MAX];

static int last_cx = -1, last_cy = -1;

static void con_draw_cell(int x, int y) {
    int c = con_buf[y * con_cols + x];
    for (int rr = 0; rr < 8; rr++)
        for (int cc = 0; cc < 8; cc++)
            fb_put((uint32_t)x * 8 + cc, (uint32_t)y * 8 + rr, CON_BG);
    if (c && c != ' ')
        k_fb_draw_char((int64_t)x * 8, (int64_t)y * 8, CON_FG, CON_BG, c);
}

static void con_draw_cursor(void) {
    if (last_cx >= 0) con_draw_cell(last_cx, last_cy);
    last_cx = con_cx; last_cy = con_cy;
    for (int yy = 0; yy < 8; yy++)
        for (int xx = 0; xx < 6; xx++)
            fb_put((uint32_t)con_cx * 8 + xx, (uint32_t)con_cy * 8 + yy, 0x2ee6d6);
}

static void con_redraw(void) {
    if (!fb_ok) return;
    k_fb_fill(CON_BG);
    for (int y = 0; y < con_rows; y++)
        for (int x = 0; x < con_cols; x++) {
            int c = con_buf[y * con_cols + x];
            if (c && c != ' ') k_fb_draw_char((int64_t)x * 8, (int64_t)y * 8, CON_FG, CON_BG, c);
        }
}

int64_t k_fb_con_init(void) {
    if (!fb_ok) return 0;
    con_cols = (int)(fb_w / 8); if (con_cols > CON_COLS_MAX) con_cols = CON_COLS_MAX;
    con_rows = (int)(fb_h / 8); if (con_rows > CON_ROWS_MAX) con_rows = CON_ROWS_MAX;
    con_cx = 0; con_cy = 0;
    for (int i = 0; i < con_cols * con_rows; i++) con_buf[i] = ' ';
    con_redraw();
    return 1;
}

void k_fb_con_clear(void) {
    for (int i = 0; i < con_cols * con_rows; i++) con_buf[i] = ' ';
    con_cx = 0; con_cy = 0;
    con_redraw();
}

static int utf8_pending = 0;   /* high byte of a 2-byte UTF-8 sequence */

void k_fb_con_putc(int64_t c) {
    if (!fb_ok) return;
    /* UTF-8: combine a 0xC0-0xDF lead byte with the following 0x80-0xBF byte */
    if (utf8_pending) {
        int cp = ((utf8_pending & 0x1F) << 6) | (c & 0x3F);
        utf8_pending = 0;
        if (cp >= 0x400) {   /* Russian letters: U+0410..U+044F */
            con_buf[con_cy * con_cols + con_cx] = cp;
            con_draw_cell(con_cx, con_cy);
            con_cx++;
            goto finish;
        }
        c = cp;              /* other: fall through as a codepoint */
    }
    if (c >= 0xC0 && c <= 0xDF) { utf8_pending = (int)c; return; }
    if (c == '\n') { con_cx = 0; con_cy++; }
    else if (c == '\b') { if (con_cx > 0) con_cx--; con_buf[con_cy * con_cols + con_cx] = ' '; con_draw_cell(con_cx, con_cy); }
    else if (c == '\t') { do { con_buf[con_cy * con_cols + con_cx] = ' '; con_cx++; } while (con_cx % 4); }
    else if (c >= 32) {
        con_buf[con_cy * con_cols + con_cx] = c;
        con_draw_cell(con_cx, con_cy);
        con_cx++;
    }
finish:
    if (con_cx >= con_cols) { con_cx = 0; con_cy++; }
    if (con_cy >= con_rows) {
        for (int i = 0; i < (con_rows - 1) * con_cols; i++) con_buf[i] = con_buf[i + con_cols];
        for (int i = (con_rows - 1) * con_cols; i < con_rows * con_cols; i++) con_buf[i] = ' ';
        con_cy = con_rows - 1;
        con_redraw();
    } else {
        con_draw_cursor();
    }
}

int64_t k_fb_con_print(const char* s) {
    if (!s) return 0;
    for (; *s; s++) k_fb_con_putc((int64_t)(unsigned char)*s);
    return 1;
}

int64_t k_fb_con_redraw(void) { con_redraw(); return 1; }

/* Dump console buffer rows 0..3 to UART as hex codepoints (diagnostic). */
void k_fb_con_dump(void) {
    const char* hx = "0123456789abcdef";
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 16; c++) {
            int v = con_buf[r * con_cols + c];
            __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)hx[(v >> 12) & 0xF]), "Nd"((uint16_t)0x3F8));
            __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)hx[(v >> 8) & 0xF]), "Nd"((uint16_t)0x3F8));
            __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)hx[(v >> 4) & 0xF]), "Nd"((uint16_t)0x3F8));
            __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)hx[v & 0xF]), "Nd"((uint16_t)0x3F8));
        }
        __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)'\n'), "Nd"((uint16_t)0x3F8));
    }
}

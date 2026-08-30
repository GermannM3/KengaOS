/*  KengaOS — десктоп UI v2: перенос refer/desktop.html на framebuffer.
    Токены и геометрия — 1:1 из CSS референса:
      topbar 38px rgba(7,10,19,.55); dock слева 44px-кнопки, radius 12/18;
      окна radius 14, бар 40px, светофор #ff5f57/#febc2e/#28c840;
      liveLog 320px справа-внизу; глоу-блобы 55vw α.16 + винетка.
    Шрифты: AA-атласы Segoe UI / Consolas (fonts/ui_font.c).
*/
#include "desktop.h"
#include "../lib/types.h"
#include "../lib/libc.h"
#include "../drivers/fb.h"
#include "../drivers/kbd.h"
#include "../drivers/ps2mouse.h"
#include "../drivers/uart.h"
#include "../arch/x86_64/io.h"
#include "../mem/buddy.h"
#include "../sched/thread.h"
#include "../sched/scheduler.h"
#include "../fs/vfs.h"
#include "../drivers/uart.h"
#include "../vmm/vmm.h"

/* ── Токены (desktop.html :root) ─────────────────────────── */
#define ACCENT    0x8B7BFF
#define ACCENT2   0x22D3EE
#define BG        0x04060B
#define TEXT      0xE8ECF8
#define DIM       0xC3CDF0
#define TOPBAR_BG 0x070A13
#define STROKE_A  20        /* rgba(255,255,255,.08) */
#define CLOSE_R   0xFF5F57
#define MIN_Y     0xFEBC2E
#define MAX_G     0x28C840
#define OK_G      0x7EE7A3
#define BOOT_TEAL 0x7EE7D0

#define TOPBAR_H  38u
#define DOCK_B    44u       /* кнопка дока */
#define WINBAR_H  40u

/* ── Состояние ───────────────────────────────────────────── */
static u32 SW, SH;
static u8  *base;                    /* обои+винетка, fb-размер */
static u8  *glow_sp[2];              /* спрайты глоу */
#define GLOW_R 352
static u32 glow_x[2], glow_y[2];

enum { A_AGENTS, A_CHAT, A_TERM, A_MONITOR, A_FILES, A_SETTINGS, A_ABOUT, A_N };
static const char *app_name[A_N] = {
    "Агенты", "Kenga Агент", "Терминал", "Системмон",
    "Файлы", "Настройки", "О системе"
};
static const char *app_tag[A_N] = {
    "K_PROC_SPAWN", "IPC · CAP_UI", "SHELL", "SYSMON",
    "INITRD", "CONFIG", "0.5 · Заря"
};
static int cur_app = -1;
static bool launcher_open = false;
static bool locked = true;
static char launch_q[40]; static u32 launch_q_len = 0;

/* Терминал: кольцевой буфер строк + ввод */
#define TERM_LINES 128
#define TERM_LINE  96
static char term_buf[TERM_LINES][TERM_LINE];
static u32  term_head = 0, term_count = 0;
static char term_in[TERM_LINE]; static u32 term_in_len = 0;

/* Чат */
#define CHAT_MSGS 10
static char chat_buf[CHAT_MSGS][TERM_LINE];
static u32  chat_head = 0, chat_count = 0;
static char chat_in[TERM_LINE]; static u32 chat_in_len = 0;

/* Живой лог (справа-внизу) */
#define LOG_N 8
static char log_buf[LOG_N][64];
static u8   log_col[LOG_N];          /* 0 dim, 1 accent2, 2 accent, 3 ok */
static u32  log_n = 0;

/* Тосты */
#define TOAST_N 3
static char toast_txt[TOAST_N][56];
static u64  toast_born[TOAST_N];
static u32  toast_n = 0;

/* Курсор: восстановление фона 18x18 */
static u8 cur_bg[18 * 18 * 4];
static u32 cur_px = 640, cur_py = 400, cur_shown = 0;
static u32 prev_btns = 0;

/* Часы (RTC) */
static u32 rtc_h, rtc_m, rtc_s, rtc_day, rtc_mon, rtc_y;
static u64 last_rtc_tick = 0;

extern void shell_execute(const char *line);   /* kmain.c */

/* ── Утилиты ─────────────────────────────────────────────── */
/* подстрока (байтовый поиск, для фильтра лончера и чата) */
static bool has_sub(const char *hay, const char *needle) {
    if (!needle[0]) return true;
    for (u32 i = 0; hay[i]; i++) {
        u32 j = 0;
        while (needle[j] && hay[i + j] == needle[j]) j++;
        if (!needle[j]) return true;
    }
    return false;
}
static void fmt_u64(u64 v, char *out) {
    char t[20]; u32 n = 0;
    if (!v) { out[0] = '0'; out[1] = 0; return; }
    while (v && n < 19) { t[n++] = '0' + (v % 10); v /= 10; }
    for (u32 i = 0; i < n; i++) out[i] = t[n - 1 - i];
    out[n] = 0;
}

static void log_push(const char *s, u8 col) {
    u32 i = log_n % LOG_N;
    u32 j = 0;
    while (s[j] && j < 62) { log_buf[i][j] = s[j]; j++; }
    log_buf[i][j] = 0;
    log_col[i] = col;
    log_n++;
}

static void toast(const char *s) {
    if (toast_n < TOAST_N) {
        u32 i = toast_n++;
        u32 j = 0;
        while (s[j] && j < 54) { toast_txt[i][j] = s[j]; j++; }
        toast_txt[i][j] = 0;
        toast_born[i] = sched_ticks();
    }
}

/* RTC через порт 0x70/0x71 (BCD). */
static u8 rtc_reg(u8 r) {
    outb(0x70, r);
    return inb(0x71);
}
static u8 bcd(u8 v) { return (u8)((v & 0x0F) + (v >> 4) * 10); }
static void rtc_read(void) {
    rtc_s = bcd(rtc_reg(0x00));
    rtc_m = bcd(rtc_reg(0x02));
    rtc_h = bcd(rtc_reg(0x04));
    rtc_day = bcd(rtc_reg(0x07));
    rtc_mon = bcd(rtc_reg(0x08));
    rtc_y  = (u32)bcd(rtc_reg(0x09)) + 2000;
}

static const char *mon_name(u32 m) {
    static const char *names[] = { "янв", "фев", "мар", "апр", "мая", "июн",
                                   "июл", "авг", "сен", "окт", "ноя", "дек" };
    return (m >= 1 && m <= 12) ? names[m - 1] : "?";
}

/* ── Обои: градиент + звёзды + винетка (пре-рендер в base) ── */
static u32 rnd_state = 0x12345678;
static u32 rnd(void) {
    rnd_state = rnd_state * 1664525u + 1013904223u;
    return rnd_state >> 8;
}

static void build_base(void) {
    /* градиент: BG → чуть светлее наверху-справа, имитация фото-обоев */
    for (u32 y = 0; y < SH; y++) {
        u32 t = (y * 255) / (SH - 1);
        u32 r = 4 + (10 * t) / 255, g = 6 + (12 * t) / 255, b = 11 + (26 * t) / 255;
        u8 *row = base + y * (SW * 4);
        for (u32 x = 0; x < SW; x++) {
            u32 u = (x * 255) / (SW - 1);
            u32 o = x * 4;
            row[o + 0] = (u8)(b + (6 * u) / 255);   /* синий канал в XRGB */
            row[o + 1] = (u8)g;
            row[o + 2] = (u8)(r + (14 * u) / 255);
            row[o + 3] = 0xFF;
        }
    }
    /* звёзды (как на primer.jpg) */
    for (u32 i = 0; i < 260; i++) {
        u32 sx = rnd() % SW, sy = rnd() % SH;
        u32 br = 40 + rnd() % 150;
        u8 *p = base + (sy * SW + sx) * 4;
        u32 r = p[2] + br / 2, g = p[1] + br / 2, b2 = p[0] + br;
        p[0] = (u8)(b2 > 255 ? 255 : b2);
        p[1] = (u8)(g > 255 ? 255 : g);
        p[2] = (u8)(r > 255 ? 255 : r);
    }
    /* винетка: radial 120% 90% at 50% 40%, прозрачн 55% → rgba(0,0,0,.45) */
    for (u32 y = 0; y < SH; y++)
        for (u32 x = 0; x < SW; x++) {
            i64 dx = (i64)x - (i64)SW / 2;
            i64 dy = (i64)y - (i64)SH * 4 / 10;
            u64 d2 = (u64)(dx * dx * 100 + dy * dy * 149);
            u64 rx = SW, ry = SH;
            u64 edge = (rx * rx * 100 + ry * ry * 149) / 100;
            if (d2 > edge / 2) {   /* 55% */
                u32 k = (u32)(((d2 - edge / 2) * 255) / (edge - edge / 2));
                if (k > 255) k = 255;
                u32 dark = (k * 45) / 100;   /* до .45 */
                u8 *p = base + (y * SW + x) * 4;
                p[0] = (u8)(p[0] * (255 - dark) / 255);
                p[1] = (u8)(p[1] * (255 - dark) / 255);
                p[2] = (u8)(p[2] * (255 - dark) / 255);
            }
        }
}

static void build_glows(void) {
    for (int s = 0; s < 2; s++) {
        glow_sp[s] = (u8 *)phys_to_virt(buddy_alloc_pages((GLOW_R * GLOW_R + 4095) / 4096));
        for (u32 y = 0; y < GLOW_R; y++)
            for (u32 x = 0; x < GLOW_R; x++) {
                i64 dx = (i64)x - GLOW_R / 2, dy = (i64)y - GLOW_R / 2;
                i64 d2 = dx * dx + dy * dy;
                i64 r2 = (GLOW_R / 2) * (GLOW_R / 2);
                u32 v = 0;
                if (d2 < r2) {
                    u64 k = 255 - (u32)((u64)d2 * 255 / (u64)r2);   /* 0..255 */
                    v = (u32)(k * k / 255 * 235 / 255);
                }
                glow_sp[s][y * GLOW_R + x] = (u8)v;
            }
    }
    glow_x[0] = SW * 30 / 100; glow_y[0] = SH * 30 / 100;   /* accent, верх-лево */
    glow_x[1] = SW * 70 / 100; glow_y[1] = SH * 74 / 100;   /* accent2, низ-право */
}

/* ── Примитивы поверх fb ─────────────────────────────────── */
static void line_h(u32 x, u32 y, u32 len, u32 rgb, u8 a) {
    fb_blend_rect(x, y, len, 1, rgb, a);
}
static void line_v(u32 x, u32 y, u32 len, u32 rgb, u8 a) {
    fb_blend_rect(x, y, 1, len, rgb, a);
}
static void circle_outline(u32 cx, u32 cy, i32 r, u32 rgb, u8 a) {
    i32 x = r, y = 0, err = 1 - r;
    while (x >= y) {
        fb_px((u32)(cx + x), (u32)(cy + y), rgb, a);
        fb_px((u32)(cx + y), (u32)(cy + x), rgb, a);
        fb_px((u32)(cx - y), (u32)(cy + x), rgb, a);
        fb_px((u32)(cx - x), (u32)(cy + y), rgb, a);
        fb_px((u32)(cx - x), (u32)(cy - y), rgb, a);
        fb_px((u32)(cx - y), (u32)(cy - x), rgb, a);
        fb_px((u32)(cx + y), (u32)(cy - x), rgb, a);
        fb_px((u32)(cx + x), (u32)(cy - y), rgb, a);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}
static void line(u32 x0, u32 y0, u32 x1, u32 y1, u32 rgb, u8 a) {
    i32 dx = (i32)x1 - (i32)x0, dy = (i32)y1 - (i32)y0;
    i32 sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    u32 adx = dx > 0 ? dx : -dx, ady = dy > 0 ? dy : -dy;
    i32 err = (i32)(adx > ady ? adx : -ady) / 2;
    for (;;) {
        fb_px(x0, y0, rgb, a);
        if (x0 == x1 && y0 == y1) break;
        i32 e2 = err;
        if (e2 > -(i32)adx) { err -= (i32)ady; x0 = (u32)((i32)x0 + sx); }
        if (e2 < (i32)ady)  { err += (i32)adx; y0 = (u32)((i32)y0 + sy); }
    }
}

/* Гексагон-логотип (как favicon референса: контур accent + K из accent2) */
static void draw_logo(u32 x, u32 y, u32 s, u8 a) {
    /* гексагон */
    u32 m = s / 2;
    line(x + m, y, x + s, y + s / 4, ACCENT, a);
    line(x + s, y + s / 4, x + s, y + s * 3 / 4, ACCENT, a);
    line(x + s, y + s * 3 / 4, x + m, y + s, ACCENT, a);
    line(x + m, y + s, x, y + s * 3 / 4, ACCENT, a);
    line(x, y + s * 3 / 4, x, y + s / 4, ACCENT, a);
    line(x, y + s / 4, x + m, y, ACCENT, a);
    /* K */
    line_v(x + s * 2 / 5, y + s / 4, s / 2, ACCENT2, a);
    line(x + s * 2 / 5, y + s / 2, x + s * 4 / 5, y + s / 4, ACCENT2, a);
    line(x + s * 2 / 5, y + s / 2, x + s * 4 / 5, y + s * 3 / 4, ACCENT2, a);
}

/* Иконки дока/ланчера: 1.5px-линии #c3cdf0 (упрощённые SVG референса) */
static void draw_icon(int app, u32 x, u32 y, u32 s, u32 rgb, u8 a) {
    u32 m = s / 6;
    switch (app) {
    case A_AGENTS: {  /* гексагон */
        u32 mm = s / 2;
        line(x + mm, y + m, x + s - m, y + s / 4, rgb, a);
        line(x + s - m, y + s / 4, x + s - m, y + s - s / 4, rgb, a);
        line(x + s - m, y + s - s / 4, x + mm, y + s - m, rgb, a);
        line(x + mm, y + s - m, x + m, y + s - s / 4, rgb, a);
        line(x + m, y + s - s / 4, x + m, y + s / 4, rgb, a);
        line(x + m, y + s / 4, x + mm, y + m, rgb, a);
        break; }
    case A_CHAT:      /* пузырь + хвост */
        fb_round_rect_outline(x + m, y + m, s - 2 * m, s - 2 * m, 6, rgb, a);
        line(x + m + 2, y + s - m, x + m, y + s - 2, rgb, a);
        line(x + m, y + s - 2, x + m + 6, y + s - m, rgb, a);
        fb_circle(x + s / 2, y + s / 2, 1, rgb, a);
        break;
    case A_TERM:      /* рамка + ❯_ */
        fb_round_rect_outline(x + m, y + m, s - 2 * m, s - 2 * m, 4, rgb, a);
        line(x + m + 4, y + s / 2 - 2, x + m + 8, y + s / 2 + 1, rgb, a);
        line(x + m + 8, y + s / 2 + 1, x + m + 4, y + s / 2 + 4, rgb, a);
        line_h(x + m + 11, y + s / 2 + 3, 5, rgb, a);
        break;
    case A_MONITOR:   /* пульс */
        fb_round_rect_outline(x + m, y + m, s - 2 * m, s - 2 * m, 4, rgb, a);
        line(x + m + 3, y + s / 2, x + m + 7, y + s / 2, rgb, a);
        line(x + m + 7, y + s / 2, x + m + 9, y + s / 2 - 4, rgb, a);
        line(x + m + 9, y + s / 2 - 4, x + m + 12, y + s / 2 + 4, rgb, a);
        line(x + m + 12, y + s / 2 + 4, x + m + 14, y + s / 2, rgb, a);
        line(x + m + 14, y + s / 2, x + s - m - 3, y + s / 2, rgb, a);
        break;
    case A_FILES:     /* папка */
        line(x + m, y + m + 3, x + m + 6, y + m + 3, rgb, a);
        line(x + m, y + m + 3, x + m, y + s - m, rgb, a);
        line(x + m, y + s - m, x + s - m, y + s - m, rgb, a);
        line(x + s - m, y + s - m, x + s - m, y + m + 6, rgb, a);
        line(x + s - m, y + m + 6, x + m + 6, y + m + 6, rgb, a);
        line(x + m + 6, y + m + 6, x + m + 4, y + m + 3, rgb, a);
        break;
    case A_SETTINGS:  /* шестерёнка: круг + спицы */
        circle_outline(x + s / 2, y + s / 2, s / 4, rgb, a);
        line_v(x + s / 2, y + m, 4, rgb, a);
        line_v(x + s / 2, y + s - m - 4, 4, rgb, a);
        line_h(x + m, y + s / 2, 4, rgb, a);
        line_h(x + s - m - 4, y + s / 2, 4, rgb, a);
        break;
    case A_ABOUT:     /* i */
        circle_outline(x + s / 2, y + s / 2, s / 3, rgb, a);
        fb_circle(x + s / 2, y + s / 3 + 1, 1, rgb, a);
        line_v(x + s / 2, y + s / 2, s / 4, rgb, a);
        break;
    }
}

/* ── Курсор ──────────────────────────────────────────────── */
static void cursor_save(void) {
    u8 *fb = fb_mem_ptr();
    for (u32 y = 0; y < 18; y++)
        for (u32 x = 0; x < 18; x++) {
            u32 px = cur_px + x, py = cur_py + y;
            u8 *d = cur_bg + (y * 18 + x) * 4;
            if (px < SW && py < SH) {
                u8 *s = fb + (py * SW + px) * 4;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            } else d[0] = d[1] = d[2] = d[3] = 0;
        }
    cur_shown = 1;
}
static void cursor_restore(void) {
    if (!cur_shown) return;
    u8 *fb = fb_mem_ptr();
    for (u32 y = 0; y < 18; y++)
        for (u32 x = 0; x < 18; x++) {
            u32 px = cur_px + x, py = cur_py + y;
            if (px >= SW || py >= SH) continue;
            u8 *d = cur_bg + (y * 18 + x) * 4;
            u8 *s = fb + (py * SW + px) * 4;
            s[0] = d[0]; s[1] = d[1]; s[2] = d[2]; s[3] = d[3];
        }
    cur_shown = 0;
}
static void cursor_draw(void) {
    static const u8 arrow[12][12] = {
        {1,0,0,0,0,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,0,0,0,0,0,0,0,0,0}, {1,1,1,1,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,0,0,0,0,0,0,0}, {1,1,1,1,1,1,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,0,0,0,0,0}, {1,1,1,1,1,1,1,1,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,0,0,0}, {1,1,1,1,1,1,1,1,1,1,0,0},
        {1,1,1,1,0,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0,0,0,0,0}
    };
    for (u32 r = 0; r < 12; r++)
        for (u32 c = 0; c < 12; c++)
            if (arrow[r][c]) fb_px(cur_px + c, cur_py + r, TEXT, 240);
    cur_shown = 1;
}

/* ── Слои ────────────────────────────────────────────────── */
static void compose(void) {
    kmemcpy((u8 *)fb_target(), base, SW * SH * 4);
    /* глоу-блобы: opacity .16 → альфа 41, screen-бленд ~ альфа */
    fb_blit_alpha(glow_sp[0], GLOW_R, GLOW_R, GLOW_R,
                  glow_x[0] - GLOW_R / 2, glow_y[0] - GLOW_R / 2, ACCENT, 41);
    fb_blit_alpha(glow_sp[1], GLOW_R, GLOW_R, GLOW_R,
                  glow_x[1] - GLOW_R / 2, glow_y[1] - GLOW_R / 2, ACCENT2, 37);
}

static void chip(u32 x, u32 y, const char *txt, u32 fg) {
    u32 w = fb_text_k_width(&kf_ui, txt);
    u32 cw = w + 18;
    fb_blend_rect(x, y, cw, 20, 0xFFFFFF, 15);
    fb_round_rect_outline(x, y, cw, 20, 8, 0xFFFFFF, 15);
    fb_text_k(&kf_ui, x + 9, y + 4, txt, fg, 210);
}
static u32 chip_w(const char *txt) { return fb_text_k_width(&kf_ui, txt) + 18; }

static void draw_topbar(void) {
    fb_blend_rect(0, 0, SW, TOPBAR_H, TOPBAR_BG, 140);
    line_h(0, TOPBAR_H - 1, SW, 0xFFFFFF, 15);

    /* лого + KENGAOS + chip */
    draw_logo(10, 5, 28, 255);
    fb_text_k(&kf_uib, 46, 12, "KENGAOS", 0xFFFFFF, 255);
    chip(46 + fb_text_k_width(&kf_uib, "KENGAOS") + 10, 9, "0.5 · agent-native", 0xCFD8F2);

    /* часы в центре */
    char clk[24], date[24];
    clk[0] = '0' + rtc_h / 10; clk[1] = '0' + rtc_h % 10; clk[2] = ':';
    clk[3] = '0' + rtc_m / 10; clk[4] = '0' + rtc_m % 10; clk[5] = ':';
    clk[6] = '0' + rtc_s / 10; clk[7] = '0' + rtc_s % 10; clk[8] = 0;
    u32 cw = fb_text_k_width(&kf_mono, clk);
    fb_text_k(&kf_mono, SW / 2 - cw / 2, 11, clk, 0xFFFFFF, 255);
    {
        char d[24]; u32 p = 0;
        d[p++] = '0' + rtc_day / 10; d[p++] = '0' + rtc_day % 10; d[p++] = ' ';
        const char *mn = mon_name(rtc_mon);
        while (*mn) d[p++] = *mn++;
        d[p++] = ' ';
        fmt_u64(rtc_y, &d[p]);
        u32 dw = fb_text_k_width(&kf_ui, d);
        fb_text_k(&kf_ui, SW / 2 - dw / 2, 13, d, 0xFFFFFF, 110);
        (void)date;
    }

    /* правые чипы: TASKS n, RAM бар+%, uptime */
    struct buddy_stats st; buddy_stats_get(&st);
    u64 used = st.allocated_pages, tot = st.total_pages;
    u32 pct = tot ? (u32)(used * 100 / tot) : 0;
    char t1[24], t2[24];
    {
        struct thread *list[32];
        u64 n = thread_enumerate(list, 32);
        char nb[8]; fmt_u64(n, nb);
        for (u32 i = 0; i < 8; i++) t1[i] = 0;
        const char *pre = "TASKS ";
        u32 p = 0; while (pre[p]) { t1[p] = pre[p]; p++; }
        u32 q = 0; while (nb[q]) t1[p++] = nb[q++];
        t1[p] = 0;
    }
    for (u32 i = 0; i < 24; i++) t2[i] = 0;
    {
        const char *pre = "RAM ";
        u32 p = 0; while (pre[p]) { t2[p] = pre[p]; p++; }
        t2[p++] = '0' + pct / 100; t2[p++] = '0' + (pct / 10) % 10; t2[p++] = '0' + pct % 10;
        t2[p++] = '%'; t2[p] = 0;
    }
    u32 x = SW - 10;
    /* power-иконка */
    x -= 28;
    {
        circle_outline(x + 14, 19, 7, 0xCFD8F2, 200);
        line_v(x + 14, 6, 7, 0xCFD8F2, 220);
        /* разрыв кольца сверху — упрощённо: пиксели фона поверх */
        line_h(x + 11, 12, 7, TOPBAR_BG, 255);
    }
    /* uptime */
    u64 sec = sched_ticks() / 100;
    char up[24], nb[10];
    fmt_u64(sec / 60, nb);
    for (u32 i = 0; i < 24; i++) up[i] = 0;
    { u32 p = 0; const char *pr = "UP "; while (pr[p]) { up[p] = pr[p]; p++; }
      u32 q = 0; while (nb[q]) up[p++] = nb[q++]; up[p++] = 'm'; up[p] = 0; }
    x -= chip_w(up) + 6; chip(x, 9, up, 0xCFD8F2);
    /* RAM с баром 52x4 */
    {
        u32 cw2 = chip_w(t2) + 60;
        x -= cw2 + 6;
        fb_blend_rect(x, 9, cw2, 20, 0xFFFFFF, 15);
        fb_round_rect_outline(x, 9, cw2, 20, 8, 0xFFFFFF, 15);
        u32 tx = x + 9;
        tx += fb_text_k(&kf_ui, tx, 12, "RAM", 0xCFD8F2, 210) + 4;
        fb_blend_rect(tx, 16, 52, 4, 0xFFFFFF, 30);
        u32 fill = 52 * pct / 100;
        if (fill > 52) fill = 52;
        /* градиент accent→accent2 упрощённо: два сегмента */
        if (fill) fb_blend_rect(tx, 16, fill / 2, 4, ACCENT, 230);
        if (fill > 1) fb_blend_rect(tx + fill / 2, 16, fill - fill / 2, 4, ACCENT2, 230);
        fb_text_k(&kf_ui, tx + 56, 12, &t2[4], 0xFFFFFF, 230);
    }
    x -= chip_w(t1) + 6; chip(x, 9, t1, 0xCFD8F2);
}

static void draw_dock(void) {
    /* dock: left:10px, центр по вертикали, glass, padding 10, gap 6 */
    u32 dh = A_N * DOCK_B + (A_N - 1) * 6 + 20;
    u32 dy = SH / 2 - dh / 2;
    fb_blend_rect(10, dy, DOCK_B + 20, dh, 0x161C30, 200);
    fb_round_rect_outline(10, dy, DOCK_B + 20, dh, 18, 0xFFFFFF, STROKE_A);
    for (int i = 0; i < A_N; i++) {
        u32 by = dy + 10 + i * (DOCK_B + 6);
        u32 bx = 10 + 10;
        int active = (i == cur_app);
        if (active) {
            fb_blend_rect(bx, by, DOCK_B, DOCK_B, 0xFFFFFF, 26);
            fb_round_rect_outline(bx, by, DOCK_B, DOCK_B, 12, 0xFFFFFF, 36);
        }
        draw_icon(i, bx + 4, by + 4, DOCK_B - 8, active ? 0xFFFFFF : DIM, active ? 255 : 200);
        if (i == A_TERM) {   /* running dot у терминала */
            fb_circle(bx + DOCK_B / 2, by + DOCK_B - 6, 2, ACCENT2, 255);
        }
    }
}

/* ── Окно ────────────────────────────────────────────────── */
static u32 win_x, win_y, win_w, win_h;
static void win_geo(int app) {
    static const u32 dims[A_N][2] = {
        {620, 430}, {470, 470}, {640, 440}, {560, 400},
        {520, 400}, {480, 380}, {520, 380}
    };
    win_w = dims[app][0]; win_h = dims[app][1];
    if (win_w > SW - 140) win_w = SW - 140;
    if (win_h > SH - 120) win_h = SH - 120;
    win_x = 96 + (SW - 96 - 360 - win_w) / 2;
    win_y = (SH - win_h) / 2 - 10;
}

static void draw_window(void) {
    if (cur_app < 0) return;
    win_geo(cur_app);
    /* тень + стекло */
    fb_blend_rect(win_x + 6, win_y + 8, win_w, win_h, 0x000000, 60);
    fb_blend_rect(win_x, win_y, win_w, win_h, 0x161C30, 205);
    fb_round_rect_outline(win_x, win_y, win_w, win_h, 14, ACCENT, 90);
    /* бар */
    fb_blend_rect(win_x + 1, win_y + 1, win_w - 2, WINBAR_H, 0xFFFFFF, 10);
    line_h(win_x + 1, win_y + WINBAR_H, win_w - 2, 0xFFFFFF, 15);
    /* светофор */
    u32 ly = win_y + WINBAR_H / 2;
    fb_circle(win_x + 22, ly, 6, CLOSE_R, 235);
    fb_circle(win_x + 41, ly, 6, MIN_Y, 235);
    fb_circle(win_x + 60, ly, 6, MAX_G, 235);
    /* заголовок + тег */
    fb_text_k(&kf_uib, win_x + 78, win_y + 13, app_name[cur_app], 0xDFE6FF, 255);
    u32 tw = fb_text_k_width(&kf_mono, app_tag[cur_app]);
    u32 tx = win_x + win_w - tw - 24;
    fb_blend_rect(tx - 7, win_y + 11, tw + 14, 18, 0xFFFFFF, 12);
    fb_round_rect_outline(tx - 7, win_y + 11, tw + 14, 18, 6, 0xFFFFFF, 26);
    fb_text_k(&kf_mono, tx, win_y + 14, app_tag[cur_app], 0xFFFFFF, 90);

    u32 bx = win_x + 12, by = win_y + WINBAR_H + 8;
    u32 bw = win_w - 24, bh = win_h - WINBAR_H - 16;
    fb_blend_rect(bx, by, bw, bh, 0x080B14, 90);

    switch (cur_app) {
    case A_AGENTS: {
        struct thread *list[24];
        u64 n = thread_enumerate(list, 24);
        u32 yy = by + 12;
        char nb[12];
        for (u64 i = 0; i < n && yy < by + bh - 16; i++, yy += 24) {
            fb_circle(bx + 12, yy + 8, 4, i ? ACCENT2 : OK_G, 255);
            char line[64];
            for (u32 k = 0; k < 64; k++) line[k] = 0;
            const char *nm = list[i]->name;
            u32 p = 0;
            while (*nm && p < 40) line[p++] = *nm++;
            line[p++] = ' ';
            fmt_u64(list[i]->id, nb);
            for (u32 k = 0; nb[k]; k++) line[p++] = nb[k];
            const char *st = " RUNNING";
            for (u32 k = 0; st[k]; k++) line[p++] = st[k];
            fb_text_k(&kf_mono, bx + 26, yy, line, i ? DIM : OK_G, 230);
        }
        /* правая панель */
        u32 px2 = bx + bw - 216;
        line_v(px2 - 8, by + 4, bh - 8, 0xFFFFFF, 18);
        fb_text_k(&kf_mono, px2, by + 12, "K_PROC_SPAWN", 0xFFFFFF, 115);
        fb_blend_rect(px2, by + 40, 190, 26, 0xFFFFFF, 14);
        fb_round_rect_outline(px2, by + 40, 190, 26, 8, 0xFFFFFF, 26);
        fb_text_k(&kf_ui, px2 + 8, by + 46, "имя агента…", 0xFFFFFF, 80);
        fb_blend_rect(px2, by + 76, 190, 26, ACCENT, 200);
        fb_round_rect_outline(px2, by + 76, 190, 26, 8, ACCENT, 230);
        u32 bw2 = fb_text_k_width(&kf_ui, "Создать агента");
        fb_text_k(&kf_ui, px2 + (190 - bw2) / 2, by + 82, "Создать агента", 0xFFFFFF, 245);
        fb_text_k(&kf_mono, px2, by + bh - 24, "дерево: init → sys → user", 0xFFFFFF, 70);
        break; }
    case A_CHAT: {
        u32 yy = by + 12;
        for (u32 i = 0; i < chat_count && yy < by + bh - 46; i++, yy += 22) {
            u32 idx = (chat_head + i) % CHAT_MSGS;
            fb_text_k(&kf_ui, bx + 8, yy, chat_buf[idx],
                      (i & 1) ? 0x9FF2E0 : DIM, 235);
        }
        line_h(bx, by + bh - 40, bw, 0xFFFFFF, 18);
        fb_blend_rect(bx + 6, by + bh - 32, bw - 60, 24, 0xFFFFFF, 14);
        fb_round_rect_outline(bx + 6, by + bh - 32, bw - 60, 24, 8, 0xFFFFFF, 26);
        fb_text_k(&kf_ui, bx + 14, by + bh - 27, chat_in, TEXT, 245);
        if ((sched_ticks() / 50) & 1)
            line_v(bx + 14 + fb_text_k_width(&kf_ui, chat_in) + 1,
                   by + bh - 27, 14, TEXT, 200);
        fb_blend_rect(bx + bw - 48, by + bh - 32, 42, 24, ACCENT, 210);
        fb_round_rect_outline(bx + bw - 48, by + bh - 32, 42, 24, 8, ACCENT, 240);
        fb_text_k(&kf_ui, bx + bw - 30, by + bh - 27, ">>", 0xFFFFFF, 245);
        break; }
    case A_TERM: {
        u32 lh = 20;
        u32 rows = (bh - 34) / lh;
        u32 start = term_count > rows ? term_count - rows : 0;
        u32 yy = by + 6;
        for (u32 i = start; i < term_count; i++, yy += lh) {
            u32 idx = (term_head + i) % TERM_LINES;
            u32 col = TEXT;
            u8 a = 210;
            if (term_buf[idx][0] == '[' && term_buf[idx][1] == 'O') { col = OK_G; a = 235; }
            if (term_buf[idx][0] == '[' && term_buf[idx][1] == '!') { col = 0xFF8F8F; a = 235; }
            if (term_buf[idx][0] == '>' ) { col = 0x9FF2E0; a = 235; }
            fb_text_k(&kf_mono, bx + 6, yy, term_buf[idx], col, a);
        }
        line_h(bx, by + bh - 26, bw, 0xFFFFFF, 18);
        fb_text_k(&kf_uib, bx + 6, by + bh - 21, ">", ACCENT, 255);
        fb_text_k(&kf_mono, bx + 22, by + bh - 20, term_in, TEXT, 245);
        if ((sched_ticks() / 50) & 1)
            line_v(bx + 22 + fb_text_k_width(&kf_mono, term_in) + 1,
                   by + bh - 20, 13, TEXT, 200);
        break; }
    case A_MONITOR: {
        struct buddy_stats st; buddy_stats_get(&st);
        u32 yy = by + 14;
        u64 used = st.allocated_pages * 4 / 1024, tot = st.total_pages * 4 / 1024;
        char l1[80], nb[12];
        for (u32 k = 0; k < 80; k++) l1[k] = 0;
        const char *p1 = "RAM: ";
        u32 p = 0; while (p1[p]) { l1[p] = p1[p]; p++; }
        fmt_u64(tot - used, nb);
        for (u32 k = 0; nb[k]; k++) l1[p++] = nb[k];
        const char *p2 = " MB free / ";
        for (u32 k = 0; p2[k]; k++) l1[p++] = p2[k];
        fmt_u64(tot, nb);
        for (u32 k = 0; nb[k]; k++) l1[p++] = nb[k];
        l1[p++] = ' '; l1[p++] = 'M'; l1[p++] = 'B';
        fb_text_k(&kf_ui, bx + 10, yy, l1, DIM, 230); yy += 30;
        u32 bwid = bw - 20;
        fb_blend_rect(bx + 10, yy, bwid, 8, 0xFFFFFF, 25);
        u32 fill = (u32)((u64)bwid * st.allocated_pages / (st.total_pages ? st.total_pages : 1));
        if (fill) fb_blend_rect(bx + 10, yy, fill / 2, 8, ACCENT, 230);
        if (fill > 1) fb_blend_rect(bx + 10 + fill / 2, yy, fill - fill / 2, 8, ACCENT2, 230);
        yy += 34;
        struct thread *list[32];
        u64 n = thread_enumerate(list, 32);
        const char *p3 = "Задач: ";
        for (u32 k = 0; k < 60; k++) l1[k] = 0;
        p = 0; while (p3[p]) { l1[p] = p3[p]; p++; }
        fmt_u64(n, nb);
        for (u32 k = 0; nb[k]; k++) l1[p++] = nb[k];
        const char *p4 = "   Тиков: ";
        for (u32 k = 0; p4[k]; k++) l1[p++] = p4[k];
        fmt_u64(sched_ticks(), nb);
        for (u32 k = 0; nb[k] && p < 74; k++) l1[p++] = nb[k];
        fb_text_k(&kf_ui, bx + 10, yy, l1, DIM, 230); yy += 30;
        const char *p5 = "Экран: ";
        for (u32 k = 0; k < 60; k++) l1[k] = 0;
        p = 0; while (p5[p]) { l1[p] = p5[p]; p++; }
        fmt_u64(SW, nb);
        for (u32 k = 0; nb[k]; k++) l1[p++] = nb[k];
        l1[p++] = 'x';
        fmt_u64(SH, nb);
        for (u32 k = 0; nb[k]; k++) l1[p++] = nb[k];
        fb_text_k(&kf_ui, bx + 10, yy, l1, DIM, 230); yy += 30;
        fb_text_k(&kf_ui, bx + 10, yy, "Все системы в норме", OK_G, 225);
        break; }
    case A_FILES: {
        static struct vfs_file *files[16];
        u64 n = vfs_list(files, 16);
        u32 yy = by + 12;
        char nb[12];
        for (u64 i = 0; i < n && yy < by + bh - 12; i++, yy += 22) {
            char row[72];
            for (u32 k = 0; k < 72; k++) row[k] = 0;
            const char *nm = files[i]->path;
            u32 p = 0;
            while (*nm && p < 40) row[p++] = *nm++;
            while (p < 44) row[p++] = ' ';
            fmt_u64(files[i]->size, nb);
            for (u32 k = 0; nb[k]; k++) row[p++] = nb[k];
            const char *suf = " B";
            for (u32 k = 0; suf[k]; k++) row[p++] = suf[k];
            fb_text_k(&kf_mono, bx + 8, yy, row, DIM, 225);
        }
        break; }
    case A_SETTINGS: {
        u32 yy = by + 16;
        fb_text_k(&kf_ui, bx + 10, yy, "Раскладка: клавиша раскладки в shell (layout)", DIM, 225); yy += 28;
        fb_text_k(&kf_ui, bx + 10, yy, "Esc — закрыть окно, M — лончер, 1-7 — приложения", DIM, 225); yy += 28;
        fb_text_k(&kf_ui, bx + 10, yy, "Терминал: полная shell-среда ядра", DIM, 225);
        break; }
    case A_ABOUT: {
        u32 cx = bx + bw / 2;
        draw_logo(cx - 28, by + 30, 56, 255);
        u32 tw2 = fb_text_k_width(&kf_uib, "KENGAOS 0.5 · Заря");
        fb_text_k(&kf_uib, cx - tw2 / 2, by + 110, "KENGAOS 0.5 · Заря", 0xFFFFFF, 250);
        u32 tw3 = fb_text_k_width(&kf_ui, "agent-native ОС · язык Кэнга в ядре");
        fb_text_k(&kf_ui, cx - tw3 / 2, by + 140, "agent-native ОС · язык Кэнга в ядре", DIM, 210);
        u32 tw4 = fb_text_k_width(&kf_mono, "Limine 12 · HHDM · buddy · ring3 · kenga emit-c");
        fb_text_k(&kf_mono, cx - tw4 / 2, by + 176, "Limine 12 · HHDM · buddy · ring3 · kenga emit-c",
                  0xFFFFFF, 110);
        break; }
    }
}

/* ── liveLog (правый-низ, 320px) ─────────────────────────── */
static void draw_livelog(void) {
    u32 w = 320, x = SW - w - 10, y = SH - 10 - (u32)(LOG_N * 18 + 38);
    fb_blend_rect(x, y, w, LOG_N * 18 + 38, 0x161C30, 195);
    fb_round_rect_outline(x, y, w, LOG_N * 18 + 38, 14, 0xFFFFFF, STROKE_A);
    fb_circle(x + 18, y + 19, 3, OK_G, (sched_ticks() / 40) & 1 ? 255 : 60);
    fb_text_k(&kf_mono, x + 30, y + 13, "ЖИВОЙ ЛОГ · ЯДРО", 0xFFFFFF, 140);
    line_h(x + 1, y + 32, w - 2, 0xFFFFFF, 15);
    u32 yy = y + 40;
    u32 start = log_n > LOG_N ? log_n - LOG_N : 0;
    for (u32 i = start; i < log_n; i++, yy += 18) {
        u32 idx = i % LOG_N;
        u32 col = 0xFFFFFF;
        u8 a = 140;
        if (log_col[idx] == 1) { col = ACCENT2; a = 200; }
        if (log_col[idx] == 2) { col = ACCENT;  a = 200; }
        if (log_col[idx] == 3) { col = OK_G;    a = 210; }
        fb_text_k(&kf_mono, x + 12, yy, log_buf[idx], col, a);
    }
}

/* ── Лончер ──────────────────────────────────────────────── */
static void draw_launcher(void) {
    if (!launcher_open) return;
    fb_blend_rect(0, 0, SW, SH, BG, 150);
    u32 tw = fb_text_k_width(&kf_uib, "ПРИЛОЖЕНИЯ");
    /* разрядка 5px: рисуем по буквам */
    {
        u32 x = SW / 2 - (tw + 5 * 9) / 2, yy = SH * 9 / 100;
        const char *t = "ПРИЛОЖЕНИЯ";
        while (*t) {
            char b[2] = { *t, 0 };
            x += fb_text_k(&kf_uib, x, yy, b, 0xFFFFFF, 220) + 5;
            t++;
        }
    }
    /* поиск */
    u32 fx = SW / 2 - 170, fy = SH * 9 / 100 + 44;
    fb_blend_rect(fx, fy, 340, 30, 0xFFFFFF, 16);
    fb_round_rect_outline(fx, fy, 340, 30, 9, 0xFFFFFF, 30);
    fb_text_k(&kf_ui, fx + 10, fy + 8, launch_q[0] ? launch_q : "Поиск приложений…",
              launch_q[0] ? TEXT : 0xFFFFFF, launch_q[0] ? 245 : 90);
    if ((sched_ticks() / 50) & 1 && launch_q[0])
        line_v(fx + 10 + fb_text_k_width(&kf_ui, launch_q) + 1, fy + 8, 15, TEXT, 200);
    /* сетка 4 колонки */
    u32 gx = SW / 2 - (4 * 150 + 3 * 14) / 2, gy = fy + 46;
    for (int i = 0; i < A_N; i++) {
        if (launch_q[0]) {
            /* фильтр по подстроке имени (латиница/кириллица, просто in-case) */
            if (!has_sub(app_name[i], launch_q)) continue;
        }
        u32 cx = gx + (u32)(i % 4) * 164, cy = gy + (u32)(i / 4) * 120;
        fb_blend_rect(cx, cy, 150, 104, 0xFFFFFF, 14);
        fb_round_rect_outline(cx, cy, 150, 104, 16, 0xFFFFFF, 22);
        u32 icx = cx + (150 - 46) / 2;
        fb_blend_rect(icx, cy + 10, 46, 46, ACCENT, 60);
        fb_blend_rect(icx + 8, cy + 14, 38, 42, ACCENT2, 45);
        fb_round_rect_outline(icx, cy + 10, 46, 46, 14, ACCENT2, 90);
        draw_icon(i, icx + 11, cy + 21, 24, 0xFFFFFF, 240);
        u32 nw = fb_text_k_width(&kf_ui, app_name[i]);
        fb_text_k(&kf_ui, cx + (150 - nw) / 2, cy + 70, app_name[i], TEXT, 230);
    }
}

/* ── Тосты ───────────────────────────────────────────────── */
static void draw_toasts(void) {
    for (u32 i = 0; i < toast_n; i++) {
        u64 age = sched_ticks() - toast_born[i];
        if (age > 300) {   /* 3с при 100 Гц */
            for (u32 j = i; j + 1 < toast_n; j++) {
                for (u32 k = 0; k < 56; k++) toast_txt[j][k] = toast_txt[j + 1][k];
                toast_born[j] = toast_born[j + 1];
            }
            toast_n--; i--;
            continue;
        }
        u32 w = fb_text_k_width(&kf_ui, toast_txt[i]) + 66;
        if (w < 290) w = 290;
        u32 x = SW - w - 12, y = TOPBAR_H + 10 + i * 62;
        fb_blend_rect(x, y, w, 52, 0x161C30, 215);
        fb_round_rect_outline(x, y, w, 52, 13, 0xFFFFFF, 26);
        fb_blend_rect(x + 10, y + 11, 30, 30, ACCENT, 200);
        fb_blend_rect(x + 18, y + 15, 22, 26, ACCENT2, 120);
        fb_round_rect_outline(x + 10, y + 11, 30, 30, 9, ACCENT2, 160);
        fb_circle(x + 25, y + 26, 3, 0xFFFFFF, 240);
        fb_text_k(&kf_ui, x + 50, y + 19, toast_txt[i], TEXT, 240);
    }
}

/* ── Lock-экран ──────────────────────────────────────────── */

static void draw_topbar_placeholder_dock_lock(void) {
    /* blur имитация: стеклянная заливка */
    fb_blend_rect(0, 0, SW, SH, BG, 130);
    char clk[9];
    clk[0] = '0' + rtc_h / 10; clk[1] = '0' + rtc_h % 10; clk[2] = ':';
    clk[3] = '0' + rtc_m / 10; clk[4] = '0' + rtc_m % 10; clk[5] = 0;
    u32 cw = fb_text_k_width(&kf_uib30, clk);
    fb_text_k(&kf_uib30, SW / 2 - cw / 2, SH / 2 - 60, clk, 0xFFFFFF, 250);
    char d[24]; u32 p = 0;
    d[p++] = '0' + rtc_day / 10; d[p++] = '0' + rtc_day % 10; d[p++] = ' ';
    const char *mn = mon_name(rtc_mon);
    while (*mn) d[p++] = *mn++;
    d[p++] = ' ';
    fmt_u64(rtc_y, &d[p]);
    u32 dw = fb_text_k_width(&kf_ui, d);
    fb_text_k(&kf_ui, SW / 2 - dw / 2, SH / 2 + 12, d, 0xFFFFFF, 130);
    const char *hint = "Нажмите любую клавишу";
    u32 hw = fb_text_k_width(&kf_ui, hint);
    u8 a = (sched_ticks() / 45) & 1 ? 180 : 50;
    fb_text_k(&kf_ui, SW / 2 - hw / 2, SH / 2 + 56, hint, 0xFFFFFF, a);
}

/* ── Ввод ────────────────────────────────────────────────── */
static void term_feed(const char *s, u32 len) {
    for (u32 i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\r') continue;
        if (c == '\n') {
            u32 idx = (term_head + term_count) % TERM_LINES;
            for (u32 k = 0; k < TERM_LINE; k++) term_buf[idx][k] = 0;
            if (term_count < TERM_LINES) term_count++;
            else term_head = (term_head + 1) % TERM_LINES;
        } else if (c == 0x08) {
            u32 idx = (term_head + term_count - 1) % TERM_LINES;
            if (term_count) {
                u32 l = 0;
                while (term_buf[idx][l]) l++;
                if (l) term_buf[idx][l - 1] = 0;
            }
        } else if ((u8)c >= 0x20) {
            if (!term_count) {   /* строка не открыта — открыть */
                u32 idx = (term_head + term_count) % TERM_LINES;
                for (u32 k = 0; k < TERM_LINE; k++) term_buf[idx][k] = 0;
                term_count++;
            }
            u32 idx = (term_head + term_count - 1) % TERM_LINES;
            u32 l = 0;
            while (term_buf[idx][l] && l < TERM_LINE - 1) l++;
            term_buf[idx][l] = c;
        }
    }
}

static void chat_push(const char *s) {
    u32 idx = (chat_head + chat_count) % CHAT_MSGS;
    u32 j = 0;
    while (s[j] && j < TERM_LINE - 1) { chat_buf[idx][j] = s[j]; j++; }
    chat_buf[idx][j] = 0;
    if (chat_count < CHAT_MSGS) chat_count++;
    else chat_head = (chat_head + 1) % CHAT_MSGS;
}

static void chat_answer(const char *q) {
    if (has_sub(q, "привет") || has_sub(q, "hello"))
        chat_push("Агент: Привет! Я системный агент KengaOS.");
    else if (has_sub(q, "память") || has_sub(q, "mem")) {
        struct buddy_stats st; buddy_stats_get(&st);
        char b[TERM_LINE], nb[12];
        for (u32 k = 0; k < TERM_LINE; k++) b[k] = 0;
        const char *p1 = "Агент: свободно ";
        u32 p = 0; while (p1[p]) { b[p] = p1[p]; p++; }
        fmt_u64(st.free_pages * 4 / 1024, nb);
        for (u32 k = 0; nb[k]; k++) b[p++] = nb[k];
        const char *p2 = " МБ";
        for (u32 k = 0; p2[k]; k++) b[p++] = p2[k];
        b[p] = 0;
        chat_push(b);
    } else if (has_sub(q, "время")) {
        char b[TERM_LINE];
        for (u32 k = 0; k < TERM_LINE; k++) b[k] = 0;
        const char *p1 = "Агент: ";
        u32 p = 0; while (p1[p]) { b[p] = p1[p]; p++; }
        b[p++] = '0' + rtc_h / 10; b[p++] = '0' + rtc_h % 10; b[p++] = ':';
        b[p++] = '0' + rtc_m / 10; b[p++] = '0' + rtc_m % 10; b[p] = 0;
        chat_push(b);
    } else {
        chat_push("Агент: спросите о памяти, времени или скажите привет.");
    }
}

static void spawn_agent(void) {
    char cmd[24];
    for (u32 k = 0; k < 24; k++) cmd[k] = 0;
    const char *p1 = "spawn";
    u32 p = 0; while (p1[p]) { cmd[p] = p1[p]; p++; }
    /* spawn печатает в kprintf → перехватим в терминал-буфер? Нет:
       выведем тост из лога потоков после вызова. */
    shell_execute(cmd);
    toast("Агенты созданы (spawn)");
    log_push("spawn: 2 demo tasks", 1);
}

static void open_app(int app) {
    cur_app = app;
    launcher_open = false;
    launch_q_len = 0; launch_q[0] = 0;
    char msg[64];
    for (u32 k = 0; k < 64; k++) msg[k] = 0;
    const char *p1 = "open: ";
    u32 p = 0; while (p1[p]) { msg[p] = p1[p]; p++; }
    const char *nm = app_name[app];
    /* имя на кириллице — в лог пишем тег */
    const char *tg = app_tag[app];
    while (*tg && p < 58) msg[p++] = *tg++;
    msg[p] = 0;
    log_push(msg, 2);
}

static void handle_mouse_click(u32 mx, u32 my) {
    if (locked) { locked = false; log_push("desktop: unlock", 3); return; }
    if (launcher_open) { launcher_open = false; launch_q[0] = 0; launch_q_len = 0; return; }

    /* топбар: лого-кнопка = лончер */
    if (my < TOPBAR_H) {
        if (mx < 44) { launcher_open = true; return; }
        return;
    }
    /* закрытие окна (светофор close) */
    if (cur_app >= 0) {
        win_geo(cur_app);
        i64 dx = (i64)mx - (win_x + 22), dy = (i64)my - (win_y + WINBAR_H / 2);
        if (dx * dx + dy * dy <= 64) { cur_app = -1; return; }
    }
    /* док */
    u32 dh = A_N * DOCK_B + (A_N - 1) * 6 + 20;
    u32 dy2 = SH / 2 - dh / 2;
    if (mx >= 10 && mx < 10 + DOCK_B + 20 && my >= dy2 && my < dy2 + dh) {
        i32 idx = ((i32)my - (i32)dy2 - 10) / (i32)(DOCK_B + 6);
        if (idx >= 0 && idx < A_N) open_app(idx);
        return;
    }
    /* кнопка "Создать агента" */
    if (cur_app == A_AGENTS) {
        win_geo(A_AGENTS);
        u32 px2 = win_x + 12 + (win_w - 24) - 216;
        u32 py2 = win_y + WINBAR_H + 8 + 76;
        if (mx >= px2 && mx <= px2 + 190 && my >= py2 && my <= py2 + 26) {
            spawn_agent();
            return;
        }
    }
    /* кнопка отправки чата */
    if (cur_app == A_CHAT) {
        win_geo(A_CHAT);
        u32 bx = win_x + 12;
        u32 bw = win_w - 24;
        u32 byy = win_y + WINBAR_H + 8;
        u32 bh = win_h - WINBAR_H - 16;
        if (mx >= bx + bw - 48 && mx <= bx + bw - 6 &&
            my >= byy + bh - 32 && my <= byy + bh - 8) {
            if (chat_in[0]) {
                chat_push(chat_in);
                chat_answer(chat_in);
                chat_in[0] = 0; chat_in_len = 0;
            }
            return;
        }
    }
}

static void handle_key(char c) {
    if (locked) { locked = false; log_push("desktop: unlock", 3); return; }

    if (c == 0x1B) {   /* Esc */
        if (launcher_open) { launcher_open = false; launch_q[0] = 0; launch_q_len = 0; }
        else cur_app = -1;
        return;
    }
    if (launcher_open) {
        if (c == '\n' || c == '\r') {
            /* открыть первое совпадение */
            for (int i = 0; i < A_N; i++)
                if (!launch_q[0] || has_sub(app_name[i], launch_q)) { open_app(i); return; }
            launcher_open = false;
            return;
        }
        if (c == 0x08) { if (launch_q_len) launch_q[--launch_q_len] = 0; return; }
        if ((u8)c >= 0x20 && launch_q_len < 38) launch_q[launch_q_len++] = c, launch_q[launch_q_len] = 0;
        return;
    }
    if (c >= '1' && c <= '7') { open_app(c - '1'); return; }
    if (c == 'm' || c == 'M') { launcher_open = true; return; }

    if (cur_app == A_TERM) {
        if (c == '\n' || c == '\r') {
            term_in[term_in_len] = 0;
            if (term_in[0]) {
                fb_set_console_hook(term_feed);
                shell_execute(term_in);
                fb_set_console_hook(NULL);
                /* хвост вывода без \n — закрыть строку */
                char nl = '\n';
                term_feed(&nl, 1);
            }
            term_in[0] = 0; term_in_len = 0;
            return;
        }
        if (c == 0x08) { if (term_in_len) term_in[--term_in_len] = 0; return; }
        if ((u8)c >= 0x20 && term_in_len < TERM_LINE - 1)
            term_in[term_in_len++] = c, term_in[term_in_len] = 0;
        return;
    }
    if (cur_app == A_CHAT) {
        if (c == '\n' || c == '\r') {
            if (chat_in[0]) {
                chat_push(chat_in);
                chat_answer(chat_in);
                chat_in[0] = 0; chat_in_len = 0;
            }
            return;
        }
        if (c == 0x08) { if (chat_in_len) chat_in[--chat_in_len] = 0; return; }
        if ((u8)c >= 0x20 && chat_in_len < TERM_LINE - 1)
            chat_in[chat_in_len++] = c, chat_in[chat_in_len] = 0;
        return;
    }
}

/* ── Главный цикл ────────────────────────────────────────── */
static void idle_wait(void) {
    __asm__ __volatile__("sti; hlt");
}

void ui_desktop_run(void) {
    fb_get_size(&SW, &SH);
    if (SW < 800 || SH < 600) {
        /* маленький экран — текстовый shell как раньше */
        extern void shell_run(void);
        shell_run();
        return;
    }

    u64 pages = ((u64)SW * SH * 4 + 4095) / 4096;
    base = (u8 *)phys_to_virt(buddy_alloc_pages(pages));
    u8 *scene = (u8 *)phys_to_virt(buddy_alloc_pages(pages));
    if (!base || !scene) { for (;;) __asm__ volatile("cli; hlt"); }
    build_base();
    build_glows();
    rtc_read();

    /* стартовый лог */
    log_push("init: ядро готово", 3);
    log_push("fb: разрешение применено", 1);
    log_push("kenga: emit-c модуль активен", 2);
    extern int g_disk_ok;
    extern int g_fat_ok;
    if (g_disk_ok) log_push("disk: sata r/w ok", 3);
    if (g_fat_ok) log_push("disk: fat32 r/w ok", 3);
    else log_push("disk: not present", 0);
    chat_push("Агент: Привет! Спросите о памяти или времени.");

    compose();
    draw_topbar_placeholder_dock_lock();
    cursor_save();

    u64 last_drift = 0, last_sec = 0xFFFFFFFF;
    for (;;) {
        bool dirty = false;

        /* клавиатура */
        while (kbd_has_char()) {
            char c = kbd_getc();
            if (c) { handle_key(c); dirty = true; }
        }
        /* мышь: движение */
        u32 mx = mouse_x(), my = mouse_y();
        if ((mx != cur_px || my != cur_py) && !locked) {
            cursor_restore();
            cur_px = mx; cur_py = my;
            cursor_draw();
        }
        /* мышь: клики (фронт) */
        u32 btns = mouse_buttons();
        if ((btns & 1) && !(prev_btns & 1)) {
            if (locked) { locked = false; dirty = true; log_push("desktop: unlock", 3); }
            else { handle_mouse_click(mx, my); dirty = true; }
        }
        prev_btns = btns;

        /* часы */
        u64 t = sched_ticks();
        if (t - last_rtc_tick >= 100) {
            last_rtc_tick = t;
            rtc_read();
        }
        if (rtc_s != last_sec) { last_sec = rtc_s; dirty = true; }

        /* дрейф глоу (26-30с период → ~1-2px/с) */
        if (t - last_drift >= 100) {
            last_drift = t;
            glow_x[0] += 1; glow_y[0] += (t / 100) & 1;
            glow_x[1] -= 1; glow_y[1] += ((t / 100) & 1) ? 0 : 1;
            if (glow_x[0] > SW) glow_x[0] = 0;
            dirty = true;
        }

        if (dirty) {
            fb_set_backbuffer(scene);
            compose();
            draw_topbar();
            draw_dock();
            draw_window();
            draw_livelog();
            draw_launcher();
            if (locked) draw_topbar_placeholder_dock_lock();
            cursor_save();
            fb_set_backbuffer(NULL);
            kmemcpy(fb_mem_ptr(), scene, SW * SH * 4);
            cursor_draw();
        }

        idle_wait();
    }
}

/*  KengaOS — минимальная libc-замена.
    Нам нужны: форматный вывод, строковые операции, memset/memcpy.
    Без динамической аллокации.
*/
#include "libc.h"
#include "../arch/x86_64/io.h"
#include "../drivers/uart.h"
#include "../drivers/fb.h"
#include <stdarg.h>

usize kstrlen(const char *s) {
    usize n = 0;
    while (*s++) n++;
    return n;
}

int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

int kstrncmp(const char *a, const char *b, usize n) {
    while (n > 0 && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (u8)*a - (u8)*b;
}

void kstrcpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

void kstrncpy(char *dst, const char *src, usize n) {
    while (n > 0 && *src) { *dst++ = *src++; n--; }
    while (n > 0) { *dst++ = 0; n--; }
}

void kmemset(void *p, u8 v, usize n) {
    u8 *q = (u8*)p;
    while (n--) *q++ = v;
}

void kmemcpy(void *dst, const void *src, usize n) {
    u8 *d = dst; const u8 *s = src;
    while (n--) *d++ = *s++;
}

int kmemcmp(const void *a, const void *b, usize n) {
    const u8 *p = a, *q = b;
    while (n--) {
        if (*p != *q) return *p - *q;
        p++; q++;
    }
    return 0;
}

/* ============================================================
   Форматный вывод
   ============================================================ */

typedef void (*emit_fn)(char c, void *ctx);

static void emit_uart(char c, void *ctx) {
    (void)ctx;
    uart_putc(UART_COM1, c);
}
static void emit_fb(char c, void *ctx) {
    (void)ctx;
    fb_putc(c);
}

struct dual_ctx { bool to_uart; bool to_fb; };

static void emit_dual(char c, void *ctx) {
    struct dual_ctx *d = ctx;
    if (d->to_uart) emit_uart(c, NULL);
    if (d->to_fb) emit_fb(c, NULL);
}

static void emit_u64(emit_fn f, void *ctx, u64 v, u32 base, bool upper) {
    char buf[32];
    int p = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) { f('0', ctx); return; }
    while (v > 0 && p < 31) {
        buf[p++] = digits[v % base];
        v /= base;
    }
    while (p > 0) f(buf[--p], ctx);
}

static void emit_i64(emit_fn f, void *ctx, i64 v) {
    if (v < 0) { f('-', ctx); v = -v; }
    emit_u64(f, ctx, (u64)v, 10, false);
}

static void emit_str(emit_fn f, void *ctx, const char *s) {
    if (!s) s = "(null)";
    while (*s) f(*s++, ctx);
}

static int kvformat(emit_fn f, void *ctx, const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') { f(*fmt++, ctx); continue; }
        fmt++;   /* skip % */
        bool is_long = false, is_long_long = false;
        while (*fmt == 'l') {
            if (is_long) is_long_long = true;
            is_long = true;
            fmt++;
        }
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char*);
                emit_str(f, ctx, s);
                break;
            }
            case 'd':
            case 'i': {
                i64 v;
                if (is_long_long) v = va_arg(ap, long long);
                else if (is_long) v = va_arg(ap, long);
                else v = va_arg(ap, int);
                emit_i64(f, ctx, v);
                break;
            }
            case 'u': {
                u64 v;
                if (is_long_long) v = va_arg(ap, unsigned long long);
                else if (is_long) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                emit_u64(f, ctx, v, 10, false);
                break;
            }
            case 'x': {
                u64 v;
                if (is_long_long) v = va_arg(ap, unsigned long long);
                else if (is_long) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                emit_u64(f, ctx, v, 16, false);
                break;
            }
            case 'X': {
                u64 v;
                if (is_long_long) v = va_arg(ap, unsigned long long);
                else if (is_long) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                emit_u64(f, ctx, v, 16, true);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                f(c, ctx);
                break;
            }
            case 'p': {
                u64 v = (u64)va_arg(ap, void*);
                f('0', ctx); f('x', ctx);
                emit_u64(f, ctx, v, 16, false);
                break;
            }
            case '%': f('%', ctx); break;
            default: f('%', ctx); f(*fmt, ctx); break;
        }
        if (*fmt) fmt++;
    }
    return 0;
}

void kprintf_uart(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvformat(emit_uart, NULL, fmt, ap);
    va_end(ap);
}

void kprintf_fb(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvformat(emit_fb, NULL, fmt, ap);
    va_end(ap);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    struct dual_ctx ctx = { .to_uart = true, .to_fb = true };
    kvformat(emit_dual, &ctx, fmt, ap);
    va_end(ap);
}

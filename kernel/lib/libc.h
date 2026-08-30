/*  KengaOS — libc-заменители: strlen, strcmp, itoa, простой printf.
*/
#ifndef KENGA_LIBC_H
#define KENGA_LIBC_H

#include "types.h"

usize kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, usize n);
void kstrcpy(char *dst, const char *src);
void kstrncpy(char *dst, const char *src, usize n);
void kmemset(void *p, u8 v, usize n);
void kmemcpy(void *dst, const void *src, usize n);
int kmemcmp(const void *a, const void *b, usize n);

/* Форматный вывод: поддерживает %s, %d, %u, %x, %llu, %c.
   Вывод идёт в UART (COM1) и в framebuffer одновременно. */
void kprintf(const char *fmt, ...);

/* Только в UART (для ранних сообщений) */
void kprintf_uart(const char *fmt, ...);

/* Только в framebuffer */
void kprintf_fb(const char *fmt, ...);

#endif

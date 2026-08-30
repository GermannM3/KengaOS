/*  KengaOS — UART 16550 (COM1).
    Используется для отладочного вывода в QEMU (-serial stdio).
    Поддерживает UTF-8 — кириллица выводится в правильных терминалах.
*/
#ifndef KENGA_UART_H
#define KENGA_UART_H

#include "../lib/types.h"
#include "io.h"

#define UART_COM1 0x3F8

/* Регистры UART, смещения от базового порта */
#define UART_DATA        0
#define UART_IER         1
#define UART_FCR         2
#define UART_LCR         3
#define UART_MCR         4
#define UART_LSR         5

#define UART_LCR_DIVLatch 0x80   /* DLAB — доступ к делителю частоты */
#define UART_LCR_8N1      0x03   /* 8 бит данных, без чётности, 1 стоп-бит */
#define UART_FCR_ENABLE   0x07   /* FIFO + очистка приёмника/передатчика */
#define UART_MCR_DTR_RTS  0x03   /* DTR + RTS */
#define UART_LSR_THRE     0x20   /* Transmit Holding Register Empty */

static inline void uart_init(u16 port) {
    /* Отключить прерывания UART */
    outb(port + UART_IER, 0x00);

    /* Включить DLAB, чтобы записать делитель */
    outb(port + UART_LCR, UART_LCR_DIVLatch | UART_LCR_8N1);
    /* Делитель 1 → 115200 бод (для QEMU это не критично, но стандарт) */
    outb(port + 0, 0x01);
    outb(port + 1, 0x00);

    /* Выключить DLAB, оставить 8N1 */
    outb(port + UART_LCR, UART_LCR_8N1);

    /* Включить FIFO, очистить, 14-байтным порогом */
    outb(port + UART_FCR, UART_FCR_ENABLE);

    /* DTR + RTS + OUT2 (нужно для прерываний, хотя мы пока поллим) */
    outb(port + UART_MCR, UART_MCR_DTR_RTS | 0x08);
}

static inline bool uart_can_send(u16 port) {
    return (inb(port + UART_LSR) & UART_LSR_THRE) != 0;
}

static inline void uart_putc(u16 port, char c) {
    while (!uart_can_send(port)) { /* spin */ }
    outb(port + UART_DATA, (u8)c);
}

/* Вывод UTF-8 строки байт-за-байтом.
   Большинство современных терминалов (включая QEMU -serial stdio)
   корректно собирают UTF-8 из последовательности байтов. */
static inline void uart_puts(u16 port, const char *s) {
    while (*s) {
        uart_putc(port, *s);
        s++;
    }
}

#endif

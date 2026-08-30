/*  KengaOS — PS/2 мышь (i8042 auxiliary port).
    Классическая инициализация контроллера 8042 + IRQ12,
    поток данных — 3-байтовые пакеты (flags, dx, dy).
*/
#include "ps2mouse.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/idt.h"
#include "../drivers/fb.h"
#include "../drivers/uart.h"

#define MOUSE_DEBUG 1

#define KBD_DATA   0x60
#define KBD_STAT   0x64
#define KBD_CMD    0x64

#define STAT_OUTBUF   0x01
#define STAT_AUX      0x20   /* байт от мыши, а не клавиатуры */

static void cmd_write(u8 cmd) { outb(KBD_CMD, cmd); }
static void data_write(u8 b)  { outb(KBD_DATA, b); }

static u8 await_read(void) {
    for (u32 i = 0; i < 100000; i++)
        if (inb(KBD_STAT) & STAT_OUTBUF) return inb(KBD_DATA);
    return 0xFF;
}

/* Послать команду мыши (через префикс 0xD4) и дождаться ACK (0xFA). */
static u8 mouse_cmd(u8 cmd) {
    cmd_write(0xD4);
    data_write(cmd);
    return await_read();   /* ACK 0xFA или 0xFE (resend) */
}

static volatile i32 cur_x = 0, cur_y = 0;
static volatile u32 cur_buttons = 0;
static u8 pkt[3];
static u32 pkt_len = 0;

static void mouse_callback(void *ctx) {
    (void)ctx;
    u8 st = inb(KBD_STAT);
#if MOUSE_DEBUG
    uart_puts(UART_COM1, "[mse] st=");
    uart_putc(UART_COM1, "0123456789ABCDEF"[(st >> 4) & 0xF]);
    uart_putc(UART_COM1, "0123456789ABCDEF"[st & 0xF]);
#endif
    if (!(st & STAT_OUTBUF)) {
#if MOUSE_DEBUG
        uart_puts(UART_COM1, " nob\r\n");
#endif
        return;
    }
    if (!(st & STAT_AUX)) {
#if MOUSE_DEBUG
        uart_puts(UART_COM1, " kb-drop\r\n");
#endif
        inb(KBD_DATA);
        return;
    }
    u8 b = inb(KBD_DATA);
#if MOUSE_DEBUG
    uart_puts(UART_COM1, " b=");
    uart_putc(UART_COM1, "0123456789ABCDEF"[(b >> 4) & 0xF]);
    uart_putc(UART_COM1, "0123456789ABCDEF"[b & 0xF]);
    uart_puts(UART_COM1, "\r\n");
#endif

    /* Синхронизация потока: бит 3 первого байта всегда 1. */
    if (pkt_len == 0 && !(b & 0x08)) return;

    pkt[pkt_len++] = b;
    if (pkt_len < 3) return;
    pkt_len = 0;

    cur_buttons = pkt[0] & 0x07;
    i32 dx = (i32)(i8)pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
    i32 dy = (i32)(i8)pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);
    dy = -dy;   /* у PS/2 положительный Y — вниз к пользователю */

    u32 sw = 0, sh = 0;
    fb_get_size(&sw, &sh);
    cur_x += dx;
    cur_y += dy;
    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
    if ((u32)cur_x > sw) cur_x = (i32)sw;
    if ((u32)cur_y > sh) cur_y = (i32)sh;
}

void ps2mouse_init(void) {
    u32 sw = 0, sh = 0;
    fb_get_size(&sw, &sh);
    cur_x = (i32)(sw / 2);
    cur_y = (i32)(sh / 2);

    /* 1. Включить auxiliary порт. */
    cmd_write(0xA8);
    await_read();

    /* 2. Конфиг-байт: разрешить IRQ12, снять блокировку тактирования мыши. */
    cmd_write(0x20);
    u8 cfg = await_read();
    cfg |= 0x03;    /* IRQ12 + IRQ1 включены */
    cfg &= (u8)~0x30;   /* мышиный такт разблокирован */
    cfg |= (u8)0x40;   /* трансляция сканкодов включена (для kbd) */
    cmd_write(0x60);
    data_write(cfg);

    /* 3. Мышь: настройки по умолчанию + разрешить передачу. */
    u8 a1 = mouse_cmd(0xF6);
    u8 a2 = mouse_cmd(0xF4);

    /* 4. Размаскировать IRQ12 в PIC2 (бит 4). */
    outb(0xA1, inb(0xA1) & (u8)~0x10);

    irq_register(12, mouse_callback);

#if MOUSE_DEBUG
    uart_puts(UART_COM1, "[mse] init ack=");
    uart_putc(UART_COM1, "0123456789ABCDEF"[(a1 >> 4) & 0xF]);
    uart_putc(UART_COM1, "0123456789ABCDEF"[a1 & 0xF]);
    uart_putc(UART_COM1, '/');
    uart_putc(UART_COM1, "0123456789ABCDEF"[(a2 >> 4) & 0xF]);
    uart_putc(UART_COM1, "0123456789ABCDEF"[a2 & 0xF]);
    uart_puts(UART_COM1, " imr=");
    u8 imr = inb(0xA1);
    uart_putc(UART_COM1, "0123456789ABCDEF"[(imr >> 4) & 0xF]);
    uart_putc(UART_COM1, "0123456789ABCDEF"[imr & 0xF]);
    uart_putc(UART_COM1, '\r'); uart_putc(UART_COM1, '\n');
#endif
}

u32 mouse_x(void) { return (u32)cur_x; }
u32 mouse_y(void) { return (u32)cur_y; }
u32 mouse_buttons(void) { return cur_buttons; }

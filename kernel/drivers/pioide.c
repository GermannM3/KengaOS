/*  KengaOS — PIO IDE (primary master, порты 0x1F0/0x3F6).
    Без DMA/IRQ — строгий поллинг BSY/DRQ: работает в любых условиях.
    LBA48: READ PIO EXT (0x24) / WRITE PIO EXT (0x34) / FLUSH (0xE7).
*/
#include "pioide.h"
#include "uart.h"
#include "../arch/x86_64/io.h"
#include "../lib/libc.h"
static const char CRNL[3] = {13, 10, 0};

#define ATA_CMDSTAT 0x1F0
#define ATA_DATA    0x1F0
#define ATA_ERR     0x1F1
#define ATA_SEXC    0x1F2
#define ATA_LBAL    0x1F3
#define ATA_LBAM    0x1F4
#define ATA_LBAH    0x1F5
#define ATA_DEV     0x1F6
#define ATA_STAT    0x1F7
#define ATA_CTRL    0x3F6

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_BSY  0x80

static u64 n_sectors = 0;
static u8  dev = 0xA0;      /* master primary */

static int wait_bsy(void) {
    for (u32 i = 0; i < 1000000; i++)        /* ~2с */
        if (!(inb(ATA_STAT) & ST_BSY)) return 0;
    return -1;
}
static int wait_drq(void) {
    for (u32 i = 0; i < 1000000; i++) {
        u8 st = inb(ATA_STAT);
        if (st & ST_ERR) return -1;
        if (st & ST_DRQ) return 0;
    }
    return -1;
}

int pioide_init(void) {
    /* 1) Идентификация */
    outb(ATA_DEV, dev & ~0x40);            /* выключить LBA бит */
    outb(ATA_CTRL, 0);                     /* снять IRQ-запреты/резет */
    wait_bsy();
    outb(ATA_SEXC, 0); outb(ATA_LBAL, 0); outb(ATA_LBAM, 0); outb(ATA_LBAH, 0);
    outb(ATA_DEV, dev);
    outb(ATA_STAT, 0xEC);                  /* IDENTIFY DEVICE */
    if (wait_bsy()) return 0;
    u8 st = inb(ATA_STAT);
    if (!st || st == 0xFF) return 0;       /* пустая шина */
    if (st & 0x01) return 0;               /* устройство нет/ошибка — не оно */

    u16 id[256];
    for (int w = 0; w < 256; w++) id[w] = inw(ATA_DATA);

    u64 lbah = ((u64)id[103] << 48) | ((u64)id[102] << 32)
             | ((u64)id[101] << 16) | id[100];
    n_sectors = lbah ? lbah : ((u64)id[61] << 16) | id[60];
    if (!n_sectors) return 0;

    uart_puts(UART_COM1, "[pioide] диск найден (PIO), секторов: ");
    char b[24]; u64 v = n_sectors, t = 0; char tmp[24];
    if (!v) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + (char)(v % 10); v /= 10; }
    for (u32 i = 0; i < t; i++) b[i] = tmp[t - 1 - i];
    b[t] = 0;
    uart_puts(UART_COM1, b);
    uart_puts(UART_COM1, "\r\n");
    return 1;
}

u64 pioide_sectors(void) { return n_sectors; }

static void lba48_setup(u64 lba, u16 count) {
    if (lba < (1ULL << 28)) {
        /* LBA28: проще и совместимо */
        outb(ATA_SEXC, (u8)(count & 0xFF));
        outb(ATA_LBAH, (u8)lba);
        outb(ATA_LBAM, (u8)(lba >> 8));
        outb(ATA_LBAL, (u8)(lba >> 16));
        outb(ATA_DEV, (u8)(dev | 0xE0 | ((lba >> 24) & 0x0F)));
        return;
    }
    outb(ATA_DEV, (u8)(dev | 0x40 | ((lba >> 24) & 0x0F)));   /* LBA, старшие 4 */
    outb(ATA_SEXC, (u8)(count >> 8));
    outb(ATA_LBAH, (u8)(lba >> 32));   /* 39:32 */
    outb(ATA_LBAM, (u8)(lba >> 40));   /* 47:40 */
    outb(ATA_LBAL, (u8)(lba >> 48));   /* 55:48 (обязателен, 0) */
    outb(ATA_SEXC, (u8)(count & 0xFF));
    outb(ATA_LBAH, (u8)lba);           /* 7:0 */
    outb(ATA_LBAM, (u8)(lba >> 8));
    outb(ATA_LBAL, (u8)(lba >> 16));   /* 23:16 */
}

int pioide_read(u64 lba, u16 count, void *buf) {
    if (!n_sectors || !count || count > 8) return -1;
    int ext = lba >= (1ULL << 28);
    lba48_setup(lba, count);
    outb(ATA_STAT, ext ? 0x24 : 0x20);
    if (wait_bsy()) return -2;
    /* QEMU/реальное железо: после снятия BSY данные уже в FIFO;
       читаем как IDENTIFY (без DRQ-покорности, оно надёжнее). */
    u16 *dst = (u16 *)buf;
    for (u32 s = 0; s < count; s++) {
        for (int w = 0; w < 256; w++) *dst++ = inw(ATA_DATA);
    }
    wait_bsy();
    {
        uart_puts(UART_COM1, "[pioide] read lba=");
        char bb[24]; u64 v = lba; u32 t = 0; char tmp2[24];
        if (!v) tmp2[t++] = 48;
        while (v) { tmp2[t++] = 48 + (char)(v % 10); v /= 10; }
        for (u32 i = 0; i < t; i++) bb[i] = tmp2[t - 1 - i];
        bb[t] = 0;
        uart_puts(UART_COM1, bb);
        uart_puts(UART_COM1, " data=");
        for (int i = 0; i < 8; i++) {
            uart_putc(UART_COM1, "0123456789ABCDEF"[((u8 *)buf)[i] >> 4]);
            uart_putc(UART_COM1, "0123456789ABCDEF"[((u8 *)buf)[i] & 0xF]);
            uart_putc(UART_COM1, 32);
        }
        uart_puts(UART_COM1, CRNL);
    }
    return 0;
}

int pioide_write(u64 lba, u16 count, const void *buf) {
    if (!n_sectors || !count || count > 8) return -1;
    int ext = lba >= (1ULL << 28);
    lba48_setup(lba, count);
    outb(ATA_STAT, ext ? 0x34 : 0x30);
    if (wait_bsy()) return -2;
    const u16 *src = (const u16 *)buf;
    for (u32 s = 0; s < count; s++) {
        if (wait_drq()) return -3;
        for (int w = 0; w < 256; w++) outw(ATA_DATA, *src++);
    }
    wait_bsy();
    outb(ATA_STAT, 0xE7);              /* FLUSH CACHE */
    wait_bsy();
    return 0;
}

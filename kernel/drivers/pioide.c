/*  KengaOS - PIO IDE (primary master, порты 0x1F0/0x3F6).
    Чистый поллинг, канонический osdev ATA PIO:
      LBA-байты -> DEV (с ниблом) -> 400нс -> команда -> BSY -> DRQ -> данные.
    Команды: LBA28 0x20/0x30, LBA48 0x24/0x34 (для >128ГиБ).
*/
#include "pioide.h"
#include "uart.h"
#include "../arch/x86_64/io.h"
#include "../lib/libc.h"

static const char CRNL[3] = {13, 10, 0};

#define ATA_DATA    0x1F0
#define ATA_ERR     0x1F1
#define ATA_SEXC    0x1F2
#define ATA_LBAL    0x1F3   /* LBA 7:0 (или 47:40 в 48-бит первый проход) */
#define ATA_LBAM    0x1F4   /* LBA 15:8 (или 55:48) */
#define ATA_LBAH    0x1F5   /* LBA 23:16 (или 63:56) */
#define ATA_DEV     0x1F6
#define ATA_STAT    0x1F7
#define ATA_CMD     0x1F7
#define ATA_CTRL    0x3F6

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_BSY  0x80

static u64 n_sectors = 0;
static u8  dev = 0xA0;      /* primary master */

static void delay400ns(void) {
    outb(0x80, 0); outb(0x80, 0); outb(0x80, 0); outb(0x80, 0);
}

static int wait_bsy(void) {
    for (u32 i = 0; i < 2000000; i++)
        if (!(inb(ATA_STAT) & ST_BSY)) return 0;
    return -1;
}

static int wait_drq(void) {
    for (u32 i = 0; i < 2000000; i++) {
        u8 st = inb(ATA_STAT);
        if (st & ST_ERR) return -1;
        if (st & ST_DRQ) return 0;
    }
    return -1;
}

int pioide_init(void) {
    /* Поллинг: прерывания диска (IRQ14) не нужны - замаскировать в PIC2.
       Иначе пост-состояние после первого WRITE сбивает шину. */
    outb(0xA1, inb(0xA1) | 0x40);

    outb(ATA_DEV, dev);
    delay400ns();
    outb(ATA_CTRL, 0);
    delay400ns();
    wait_bsy();
    outb(ATA_SEXC, 0);
    outb(ATA_LBAL, 0);
    outb(ATA_LBAM, 0);
    outb(ATA_LBAH, 0);
    outb(ATA_CMD, 0xEC);              /* IDENTIFY DEVICE */
    if (wait_bsy()) return 0;
    u8 st = inb(ATA_STAT);
    if (!st || st == 0xFF) return 0;
    if (st & ST_ERR) return 0;

    u16 id[256];
    for (int w = 0; w < 256; w++) id[w] = inw(ATA_DATA);

    u64 lbah = ((u64)id[103] << 48) | ((u64)id[102] << 32)
             | ((u64)id[101] << 16) | id[100];
    n_sectors = lbah ? lbah : (((u64)id[61] << 16) | id[60]);
    if (!n_sectors) return 0;

    uart_puts(UART_COM1, "[pioide] диск найден (PIO), секторов: ");
    char b[24]; u64 v = n_sectors, t = 0; char tmp[24];
    if (!v) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + (char)(v % 10); v /= 10; }
    for (u32 i = 0; i < t; i++) b[i] = tmp[t - 1 - i];
    b[t] = 0;
    uart_puts(UART_COM1, b);
    uart_puts(UART_COM1, CRNL);
    return 1;
}

u64 pioide_sectors(void) { return n_sectors; }

/* Регистры + device. ext=0 -> LBA28, 1 -> LBA48. */
static void setup_cmd_lba(u64 lba, u16 count, int ext) {
    if (ext) {
        outb(ATA_SEXC, (u8)(count >> 8));
        outb(ATA_LBAL, (u8)(lba >> 24));
        outb(ATA_LBAM, (u8)(lba >> 32));
        outb(ATA_LBAH, (u8)(lba >> 40));
        outb(ATA_SEXC, (u8)(count & 0xFF));
        outb(ATA_LBAL, (u8)lba);
        outb(ATA_LBAM, (u8)(lba >> 8));
        outb(ATA_LBAH, (u8)(lba >> 16));
        outb(ATA_DEV, (u8)(dev | 0x40));
    } else {
        outb(ATA_SEXC, (u8)(count & 0xFF));
        outb(ATA_LBAL, (u8)lba);
        outb(ATA_LBAM, (u8)(lba >> 8));
        outb(ATA_LBAH, (u8)(lba >> 16));
        outb(ATA_DEV, (u8)(dev | 0xE0 | ((lba >> 24) & 0x0F)));
    }
    delay400ns();
}

int pioide_read(u64 lba, u16 count, void *buf) {
    if (!n_sectors || !count || count > 8) return -1;
    if (wait_bsy()) return -2;
    int ext = lba >= (1ULL << 28);
    setup_cmd_lba(lba, count, ext);
    outb(ATA_CMD, ext ? 0x24 : 0x20);
    if (wait_bsy()) return -3;
    if (wait_drq()) return -4;
    u16 *dst = (u16 *)buf;
    for (u32 s = 0; s < count; s++) {
        for (int w = 0; w < 256; w++) *dst++ = inw(ATA_DATA);
        if (s + 1 < count) { wait_bsy(); if (wait_drq()) return -5; }
    }
    return 0;
}

int pioide_write(u64 lba, u16 count, const void *buf) {
    if (!n_sectors || !count || count > 8) return -1;
    if (wait_bsy()) return -2;
    int ext = lba >= (1ULL << 28);
    setup_cmd_lba(lba, count, ext);
    outb(ATA_CMD, ext ? 0x34 : 0x30);
    if (wait_bsy()) return -3;
    const u16 *src = (const u16 *)buf;
    for (u32 s = 0; s < count; s++) {
        if (wait_drq()) return -4;
        for (int w = 0; w < 256; w++) outw(ATA_DATA, *src++);
    }
    wait_bsy();
    return 0;
}

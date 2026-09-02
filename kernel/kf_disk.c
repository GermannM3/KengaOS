/* kf_disk.c — PIO-IDE (норм. ATA PIO): поллинг, IDENTIFY, LBA28/48 R/W.
   Портирован из legacy C-ядра (pioide.c) в единое kenga-ядро. */
/*  KengaOS - PIO IDE (primary master, порты 0x1F0/0x3F6).
    Чистый поллинг, канонический osdev ATA PIO:
      LBA-байты -> DEV (с ниблом) -> 400нс -> команда -> BSY -> DRQ -> данные.
    Команды: LBA28 0x20/0x30, LBA48 0x24/0x34 (для >128ГиБ).
*/
#include "kf_rt.h"

static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ __volatile__("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(uint16_t p, uint16_t v) { __asm__ __volatile__("outw %0,%1" : : "a"(v), "Nd"(p)); }






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

static uint64_t n_sectors = 0;
static uint8_t  dev = 0xA0;      /* primary master */

static void delay400ns(void) {
    outb(0x80, 0); outb(0x80, 0); outb(0x80, 0); outb(0x80, 0);
}

static int wait_bsy(void) {
    for (uint32_t i = 0; i < 2000000; i++)
        if (!(inb(ATA_STAT) & ST_BSY)) return 0;
    return -1;
}

static int wait_drq(void) {
    for (uint32_t i = 0; i < 2000000; i++) {
        uint8_t st = inb(ATA_STAT);
        if (st & ST_ERR) return -1;
        if (st & ST_DRQ) return 0;
    }
    return -1;
}

int64_t k_disk_init(void) {
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
    uint8_t st = inb(ATA_STAT);
    if (!st || st == 0xFF) return 0;
    if (st & ST_ERR) return 0;

    uint16_t id[256];
    for (int w = 0; w < 256; w++) id[w] = inw(ATA_DATA);

    uint64_t lbah = ((uint64_t)id[103] << 48) | ((uint64_t)id[102] << 32)
             | ((uint64_t)id[101] << 16) | id[100];
    n_sectors = lbah ? lbah : (((uint64_t)id[61] << 16) | id[60]);
    if (!n_sectors) return 0;

    char b[24]; uint64_t v = n_sectors, t = 0; char tmp[24];
    if (!v) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + (char)(v % 10); v /= 10; }
    for (uint32_t i = 0; i < t; i++) b[i] = tmp[t - 1 - i];
    b[t] = 0;
    return 1;
}

int64_t k_disk_sectors(void) { return n_sectors; }

/* Регистры + device. ext=0 -> LBA28, 1 -> LBA48. */
static void setup_cmd_lba(uint64_t lba, uint16_t count, int ext) {
    if (ext) {
        outb(ATA_SEXC, (uint8_t)(count >> 8));
        outb(ATA_LBAL, (uint8_t)(lba >> 24));
        outb(ATA_LBAM, (uint8_t)(lba >> 32));
        outb(ATA_LBAH, (uint8_t)(lba >> 40));
        outb(ATA_SEXC, (uint8_t)(count & 0xFF));
        outb(ATA_LBAL, (uint8_t)lba);
        outb(ATA_LBAM, (uint8_t)(lba >> 8));
        outb(ATA_LBAH, (uint8_t)(lba >> 16));
        outb(ATA_DEV, (uint8_t)(dev | 0x40));
    } else {
        outb(ATA_SEXC, (uint8_t)(count & 0xFF));
        outb(ATA_LBAL, (uint8_t)lba);
        outb(ATA_LBAM, (uint8_t)(lba >> 8));
        outb(ATA_LBAH, (uint8_t)(lba >> 16));
        outb(ATA_DEV, (uint8_t)(dev | 0xE0 | ((lba >> 24) & 0x0F)));
    }
    delay400ns();
}

int64_t k_disk_read(uint64_t lba, uint16_t count, void *buf) {
    if (!n_sectors || !count || count > 8) return -1;
    if (wait_bsy()) return -2;
    int ext = lba >= (1ULL << 28);
    setup_cmd_lba(lba, count, ext);
    outb(ATA_CMD, ext ? 0x24 : 0x20);
    if (wait_bsy()) return -3;
    if (wait_drq()) return -4;
    uint16_t *dst = (uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        for (int w = 0; w < 256; w++) *dst++ = inw(ATA_DATA);
        if (s + 1 < count) { wait_bsy(); if (wait_drq()) return -5; }
    }
    return 0;
}

int64_t k_disk_write(uint64_t lba, uint16_t count, const void *buf) {
    if (!n_sectors || !count || count > 8) return -1;
    if (wait_bsy()) return -2;
    int ext = lba >= (1ULL << 28);
    setup_cmd_lba(lba, count, ext);
    outb(ATA_CMD, ext ? 0x34 : 0x30);
    if (wait_bsy()) return -3;
    const uint16_t *src = (const uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (wait_drq()) return -4;
        for (int w = 0; w < 256; w++) outw(ATA_DATA, *src++);
    }
    wait_bsy();
    return 0;
}

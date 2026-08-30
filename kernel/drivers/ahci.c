/*  KengaOS — AHCI драйвер (SATA, DMA, поллинг).
    Реализация по spec 1.3.1 + практике osdev:
      ABAR из BAR5 → GHC.AE → port reset → CLB/FB/CT (по странице из buddy,
      страницы 4K-выровнены — требования 1K/256/128 выполнены) → IDENTIFY → R/W.
*/
#include "ahci.h"
#include "pci.h"
#include "uart.h"
#include "../arch/x86_64/io.h"
#include "../mem/buddy.h"
#include "../vmm/vmm.h"
#include "../lib/libc.h"
static const char AHCICRLF[3] = {13, 10, 0};
static const char CRLF[3] = {13, 10, 0};

/* ── MMIO helpers (volatile) ─────────────────────────────── */
static volatile u32 *ab;          /* ABAR как массив u32 */

static inline u32 rd(u32 off)            { return ab[off / 4]; }
static inline void wr(u32 off, u32 v)    { ab[off / 4] = v; }

/* регистры порта: база 0x100 + i*0x80 */
#define P(i, o) ((i) * 0x80 + 0x100 + (o))
#define PxCLB   0x00
#define PxCMD   0x18
#define PxTFD   0x20
#define PxSIG   0x24
#define PxSERR  0x30
#define PxCI    0x38

/* GHC биты */
#define GHC_AE   (1u << 31)
#define GHC_MRSM (1u << 2)
#define PxCMD_ST (1u << 0)
#define PxCMD_FRE (1u << 4)
#define PxCMD_FR  (1u << 8)
#define PxCMD_CR  (1u << 15)
#define PxCI_SLOT0 1u

/* ── Структуры команд (физическая память из buddy) ───────── */
typedef struct {
    volatile u32 clb_page;    /* command list (1 страница) */
    volatile u32 fis_page;    /* FIS receive (1 страница) */
    volatile u32 ct_page;     /* command table slot0 (1 страница) */
} ahci_port_mem_t;

static ahci_port_mem_t pm;
static void *f_v;
static int port_idx = -1;
static u64 n_sectors = 0;

/* ── Строим структуры в виртуальных адресах ──────────────── */
static void *cl_v, *fis_v, *ct_v;

u64 sched_ticks(void);
static void msleep(int ms) {
    /* тики планировщика: PIT 100 Гц → 1 тик = 10 мс.
       ВАЖНО: ожидание через hlt, а не pause-спин — в QEMU TCG (1 поток)
       DMA-завершение AHCI выполняется в главном цикле, pause-спин его
       не выпускает — команды «зависали», данные не доходили до диска. */
    u64 target = sched_ticks() + (u64)(ms + 9) / 10 + 1;
    __asm__ volatile("sti");
    while ((i64)sched_ticks() < (i64)target)
        __asm__ volatile("hlt");
}

/* Ждать очистки бита с таймаутом. 1 = успех. */
static int wait_clear(u32 off, u32 mask, int loops) {
    for (int i = 0; i < loops; i++) {
        if (!(rd(off) & mask)) return 1;
        msleep(10);
    }
    return (rd(off) & mask) == 0;
}
static int wait_set(u32 off, u32 mask, int loops) {
    for (int i = 0; i < loops; i++) {
        if (rd(off) & mask) return 1;
        msleep(1);
    }
    return (rd(off) & mask) != 0;
}

/* ── Заполнить command header slot0 + command table ──────── */
static void setup_cmd(int write, u16 prdtl) {
    volatile u32 *hdr = (volatile u32 *)cl_v;   /* slot0, 32 байта */
    /* Layout QEMU (ahci.c): DW0 = opts(16) | prdtl(16);
       DW1:DW2 = tbl_addr (64-битный физ. адрес command table). */
    u32 opts = (5 /*CFL=FIS 5 dword*/ & 0x1F)
             | (write ? (1u << 6) : 0)  /* W */
             | (1u << 7);  /* P:Prefetch */
    hdr[0] = ((u32)(prdtl & 0xFFFF) << 16) | (opts & 0xFFFF);
    hdr[1] = (u32)(pm.ct_page & 0xFFFFFFFF);          /* tbl_addr low */
    hdr[2] = (u32)(((u64)pm.ct_page >> 32) & 0xFFFFFFFF);
    hdr[3] = 0;
}

/* PRDT entry в CT по индексу. */
static volatile u32 *prdt(int i) {
    return (volatile u32 *)((u8 *)ct_v + 0x80 + i * 16);
}

/* Register FIS to device (48-bit LBA). */
static void fis_h2d(u8 cmd, u64 lba, u16 count, int dma_write) {
    u8 *f = (u8 *)ct_v;                 /* CFIS в начале CT */
    for (int i = 0; i < 64; i++) f[i] = 0;
    f[0] = 0x27;                        /* FIS type: Reg H2D */
    f[1] = 0x80;                        /* C=1 */
    f[2] = cmd;
    f[3] = 0;
    f[4] = (u8)lba;
    f[5] = (u8)(lba >> 8);
    f[6] = (u8)(lba >> 16);
    f[7] = 0x40;                        /* device: LBA48 */
    f[8] = (u8)(lba >> 24);
    f[9] = (u8)(lba >> 32);
    f[10] = (u8)(lba >> 40);
    f[11] = 0;
    f[12] = (u8)(count >> 8);
    f[13] = (u8)(count & 0xFF);
    f[14] = 0;
    f[15] = 0;
    (void)dma_write;
}

/* Исполнить slot0 и дождаться завершения. 1 = успех. */
static int issue_wait(void) {
    wr(P(port_idx, PxCI), PxCI_SLOT0);
    /* Ожидание снятия CI. Ожидание идёт на hlt (см. msleep) — иначе QEMU TCG
       не закрутит завершение DMA и команда повиснет вечно. */
    int ok = wait_clear(P(port_idx, PxCI), PxCI_SLOT0, 20000);
    u32 err = rd(P(port_idx, PxSERR));
    if (err) {
        wr(P(port_idx, PxSERR), err);
        ok = 0;
    }
    if (!ok) uart_puts(UART_COM1, "[dw] CI TIMEOUT\r\n");
    return ok;
}

/* ── Публичный API ───────────────────────────────────────── */
static u32 dma_page;    /* физическая страница DMA-буфера */
static u8 *dma_v;

int ahci_init(void) {
    const pci_dev_t *pc = pci_find_class(0x01, 0x06);
    if (!pc) {
        uart_puts(UART_COM1, "[ahci] контроллер не найден\r\n");
        return 0;
    }
    uart_puts(UART_COM1, "[ahci] PCI найден, ABAR=");
    u32 abar = pc->bars[5] & ~0xF;
    char hex[9];
    for (int i = 0; i < 8; i++) hex[i] = "0123456789ABCDEF"[(abar >> (28 - 4 * i)) & 0xF];
    hex[8] = 0;
    uart_puts(UART_COM1, hex);
    uart_puts(UART_COM1, "\r\n");

    ab = (volatile u32 *)phys_to_virt(abar);

    /* Включить AHCI-режим (GHC.AE). MRSM не трогаем. */
    wr(0x04, rd(0x04) | GHC_AE);
    msleep(5);

    /* Найти порт с SATA-диском */
    u32 pi = rd(0x0C);
    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) continue;
        u32 sig = rd(P(i, PxSIG));
        if (sig == 0x00000101) { port_idx = i; break; }
    }
    if (port_idx < 0) {
        uart_puts(UART_COM1, "[ahci] SATA-диск не найден (PI=");
        u32 v = pi; char t[9];
        for (int i = 0; i < 8; i++) t[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 0xF];
        t[8] = 0;
        uart_puts(UART_COM1, t);
        uart_puts(UART_COM1, ")\r\n");
        return 0;
    }
    uart_puts(UART_COM1, "[ahci] порт найден: ");
    uart_putc(UART_COM1, '0' + port_idx);
    uart_puts(UART_COM1, "\r\n");

    /* Память порта: CL / FIS / CT — по странице (4K-выравнивание) */
    pm.clb_page = (u32)buddy_alloc_pages(1);
    pm.fis_page = (u32)buddy_alloc_pages(1);
    pm.ct_page  = (u32)buddy_alloc_pages(1);
    dma_page    = (u32)buddy_alloc_pages(1);
    if (!pm.clb_page || !pm.fis_page || !pm.ct_page || !dma_page) {
        uart_puts(UART_COM1, "[ahci] нет памяти\r\n");
        return 0;
    }
    cl_v  = phys_to_virt(pm.clb_page);
    fis_v = phys_to_virt(pm.fis_page);
    f_v = fis_v;
    ct_v  = phys_to_virt(pm.ct_page);
    dma_v = phys_to_virt(dma_page);
    kmemset(cl_v, 0, 4096);
    kmemset(fis_v, 0, 4096);
    kmemset(ct_v, 0, 4096);

    /* Остановить движок, перепрограммировать адреса, запустить */
    wr(P(port_idx, PxCMD), rd(P(port_idx, PxCMD)) & ~PxCMD_ST);
    if (!wait_clear(P(port_idx, PxCMD), PxCMD_CR | PxCMD_FR, 50000)) {
        uart_puts(UART_COM1, "[ahci] CR не сбросился\r\n");
        return 0;
    }
    wr(P(port_idx, PxCLB), pm.clb_page);
    wr(P(port_idx, PxCLB + 4), 0);
    wr(P(port_idx, 0x08), pm.fis_page);      /* PxFB */
    wr(P(port_idx, 0x0C), 0);
    wr(P(port_idx, PxSERR), 0xFFFFFFFF);     /* сброс ошибок */
    /* Включить прерывания порта и хост-апгрейд: QEMU увязывает снятие
       PxCI (cmd_done) с IRQ-механизмом — без PxIE/GHC.IE команда
       завершается, но CI не очищается. */
    wr(P(port_idx, 0x14), 0xFFFFFFFF);   /* PxIE: все IRQ порта */
    wr(0x14, 1);                          /* GHC.IE */
    wr(P(port_idx, PxCMD), PxCMD_FRE | PxCMD_ST);
    msleep(50);
    {
        uart_puts(UART_COM1, "[ahci] CMD=");
        char hx[9];
        u32 vals[4];
        vals[0] = rd(P(port_idx, PxCMD));
        vals[1] = rd(P(port_idx, 0x28));   /* PxSSTS */
        vals[2] = rd(P(port_idx, PxCLB));
        vals[3] = rd(P(port_idx, 0x08));   /* PxFB */
        const char *names[4] = {"CMD=", "SSTS=", "CLB=", "FB="};
        for (int k = 0; k < 4; k++) {
            uart_puts(UART_COM1, names[k]);
            for (int i = 0; i < 8; i++) hx[i] = "0123456789ABCDEF"[(vals[k] >> (28 - 4 * i)) & 0xF];
            hx[8] = 0;
            uart_puts(UART_COM1, hx);
            uart_putc(UART_COM1, ' ');
        }
        uart_puts(UART_COM1, CRLF);
    }

    /* Ждать готовности (TFD.BSY=0, DRQ=0) */
    for (int i = 0; i < 100; i++) {
        u32 tfd = rd(P(port_idx, PxTFD));
        if (!(tfd & 0x88)) break;
        msleep(10);
    }

    /* IDENTIFY DEVICE (0xEC): 1 сектор в dma_v */
    for (int i = 0; i < 512; i++) ((u8 *)dma_v)[i] = 0xAA;
    setup_cmd(0, 1);
    prdt(0)[0] = dma_page;          /* DBA */
    prdt(0)[1] = 0;
    prdt(0)[2] = 0;
    prdt(0)[3] = (512 - 1) | (1u << 31);   /* DBC + I */
    u8 *f = (u8 *)ct_v;
    for (int i = 0; i < 64; i++) f[i] = 0;
    f[0] = 0x27; f[1] = 0x80; f[2] = 0xEC;
    if (!issue_wait()) {
        uart_puts(UART_COM1, "[ahci] IDENTIFY не прошёл\r\n");
        return 0;
    }
    /* LBA48 supported? (word 83 bit10) */
    u16 *id = (u16 *)dma_v;
    n_sectors = ((u64)id[103] << 48) | ((u64)id[102] << 32)
              | ((u64)id[101] << 16) | id[100];
    if (!n_sectors) n_sectors = id[61] << 16 | id[60];   /* LBA28 fallback */
    {
        volatile u32 *pd = (volatile u32 *)((u8 *)ct_v + 0x80);
        char hx2[9];
#define hx hx2
        uart_puts(UART_COM1, "[ahci] pages CL=");
        char ph[9];
        u32 pages[4] = { pm.clb_page, pm.fis_page, pm.ct_page, dma_page };
        for (int k = 0; k < 4; k++) {
            for (int i = 0; i < 8; i++) ph[i] = "0123456789ABCDEF"[(pages[k] >> (28 - 4 * i)) & 0xF];
            ph[8] = 0; uart_puts(UART_COM1, ph); uart_putc(UART_COM1, ' ');
        }
        uart_puts(UART_COM1, CRLF);
        uart_puts(UART_COM1, "[ahci] FB bytes:");
        for (int i = 0; i < 8; i++) {
            u8 fbv = ((u8 *)f_v)[i];
            uart_putc(UART_COM1, "0123456789ABCDEF"[(fbv >> 4) & 0xF]);
            uart_putc(UART_COM1, "0123456789ABCDEF"[fbv & 0xF]);
            uart_putc(UART_COM1, ' ');
        }
        uart_puts(UART_COM1, CRLF);
        uart_puts(UART_COM1, "[ahci] PRDT DBA=");
        for (int i = 0; i < 8; i++) hx[i] = "0123456789ABCDEF"[(pd[0] >> (28 - 4 * i)) & 0xF];
        hx[8] = 0; uart_puts(UART_COM1, hx);
        uart_puts(UART_COM1, " DW3=");
        for (int i = 0; i < 8; i++) hx[i] = "0123456789ABCDEF"[(pd[3] >> (28 - 4 * i)) & 0xF];
        uart_puts(UART_COM1, hx);
        uart_puts(UART_COM1, " dma_phys=");
        for (int i = 0; i < 8; i++) hx[i] = "0123456789ABCDEF"[(dma_page >> (28 - 4 * i)) & 0xF];
        uart_puts(UART_COM1, hx);
        uart_puts(UART_COM1, " hdr0=");
        volatile u32 *hh = (volatile u32 *)cl_v;
        for (int i = 0; i < 8; i++) hx[i] = "0123456789ABCDEF"[(hh[0] >> (28 - 4 * i)) & 0xF];
        uart_puts(UART_COM1, hx);
        uart_puts(UART_COM1, " hdr1=");
        for (int i = 0; i < 8; i++) hx[i] = "0123456789ABCDEF"[(hh[1] >> (28 - 4 * i)) & 0xF];
        uart_puts(UART_COM1, hx);
        uart_puts(UART_COM1, CRLF);
        /* повторный IDENTIFY: свежая команда */
        setup_cmd(0, 1);
        pd[0] = dma_page; pd[1] = 0; pd[2] = 0;
        pd[3] = (512 - 1) | (1u << 31);
        u8 *f2 = (u8 *)ct_v;
        for (int i = 0; i < 64; i++) f2[i] = 0;
        f2[0] = 0x27; f2[1] = 0x80; f2[2] = 0xEC;
        issue_wait();
        uart_puts(UART_COM1, "[ahci] retry id words:");
        for (int w = 0; w < 6; w++) {
            u16 wv = ((u16 *)dma_v)[w];
            uart_putc(UART_COM1, "0123456789ABCDEF"[(wv >> 12) & 0xF]);
            uart_putc(UART_COM1, "0123456789ABCDEF"[(wv >> 8) & 0xF]);
            uart_putc(UART_COM1, "0123456789ABCDEF"[(wv >> 4) & 0xF]);
            uart_putc(UART_COM1, "0123456789ABCDEF"[wv & 0xF]);
            uart_putc(UART_COM1, ' ');
        }
        uart_puts(UART_COM1, CRLF);
#undef hx
        uart_puts(UART_COM1, "[ahci] TFD=");
        char hx[9]; u32 tfd = rd(P(port_idx, PxTFD));
        for (int i = 0; i < 8; i++) hx[i] = "0123456789ABCDEF"[(tfd >> (28 - 4 * i)) & 0xF];
        hx[8] = 0; uart_puts(UART_COM1, hx);
        uart_puts(UART_COM1, " IS=");
        u32 isv = rd(P(port_idx, 0x10));
        for (int i = 0; i < 8; i++) hx[i] = "0123456789ABCDEF"[(isv >> (28 - 4 * i)) & 0xF];
        uart_puts(UART_COM1, hx);
        uart_puts(UART_COM1, " id[0..3]=");
        for (int w = 0; w < 4; w++) {
            u16 wv = ((u16 *)dma_v)[w];
            uart_putc(UART_COM1, "0123456789ABCDEF"[(wv >> 12) & 0xF]);
            uart_putc(UART_COM1, "0123456789ABCDEF"[(wv >> 8) & 0xF]);
            uart_putc(UART_COM1, "0123456789ABCDEF"[(wv >> 4) & 0xF]);
            uart_putc(UART_COM1, "0123456789ABCDEF"[wv & 0xF]);
            uart_putc(UART_COM1, ' ');
        }
        uart_puts(UART_COM1, CRLF);
    }
    uart_puts(UART_COM1, "[ahci] IDENTIFY ok, секторов: ");
    char b[24]; u64 v = n_sectors; u32 t = 0; char tmp[24];
    if (!v) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + (char)(v % 10); v /= 10; }
    for (u32 i = 0; i < t; i++) b[i] = tmp[t - 1 - i];
    b[t] = 0;
    uart_puts(UART_COM1, b);
    uart_puts(UART_COM1, "\r\n");
    return 1;
}

u64 ahci_sectors(void) { return n_sectors; }

int ahci_read(u64 lba, u16 count, void *buf) {
    if (port_idx < 0 || !count || count > 8) return -1;
    setup_cmd(0, 1);
    prdt(0)[0] = dma_page;
    prdt(0)[1] = 0;
    prdt(0)[2] = 0;
    prdt(0)[3] = ((u32)count * 512 - 1) | (1u << 31);
    fis_h2d(0x25 /* READ DMA EXT */, lba, count, 0);
    if (!issue_wait()) return -2;
    kmemcpy(buf, dma_v, (u32)count * 512);
    return 0;
}

int ahci_write(u64 lba, u16 count, const void *buf) {
    uart_puts(UART_COM1, "[dw] enter\r\n");
    if (port_idx < 0 || !count || count > 8) return -1;
    uart_puts(UART_COM1, "[dw] memcpy\r\n");
    kmemcpy(dma_v, buf, (u32)count * 512);
    setup_cmd(1, 1);
    prdt(0)[0] = dma_page;
    prdt(0)[1] = 0;
    prdt(0)[2] = 0;
    prdt(0)[3] = ((u32)count * 512 - 1) | (1u << 31);
    uart_puts(UART_COM1, "[dw] fis ready\r\n");
    fis_h2d(0x35 /* WRITE DMA EXT */, lba, count, 1);
    if (!issue_wait()) return -2;
    /* дождаться записи: cache flush (FLUSH CACHE EXT 0xEA) */
    setup_cmd(0, 0);
    u8 *f = (u8 *)ct_v;
    for (int i = 0; i < 64; i++) f[i] = 0;
    f[0] = 0x27; f[1] = 0x80; f[2] = 0xEA;
    issue_wait();
    return 0;
}

/* kf_nvme.c — NVMe (PCIe SSD) диск для x86_64.
 *
 * Зачем: на новых ноутбуках диск — NVMe, и ни legacy ATA PIO, ни AHCI его
 * не видят. NVMe — это PCIe-устройство с MMIO-регистрами и очередями:
 * admin SQ/CQ для управления и I/O SQ/CQ для данных, всё через DMA.
 * Поллинг (прерывания не нужны), без метаданных и без SGL.
 *
 * Передачи: одна команда = не больше одной страницы (4 КиБ), поэтому хватает
 * PRP1. Логический блок NVMe обычно 512 Б (QEMU), но бывает и 4096 —
 * k_nvme_read/write работают в 512-байтных секторах ядра и при LBA 4096
 * собирают/разбирают блок (для записи — read-modify-write).
 *
 * ponytail: один контроллер, одно пространство имён (NSID 1), одна пара
 * I/O-очередей, поллинг. Этого достаточно, чтобы KengaFS жила на NVMe.
 */
#include "kf_rt.h"

/* --- PCI config (0xCF8/0xCFC) --- */
static inline uint8_t  n_inb(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void     n_outb(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint32_t n_inl(uint16_t p) { uint32_t v; __asm__ __volatile__("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void     n_outl(uint16_t p, uint32_t v) { __asm__ __volatile__("outl %0,%1" : : "a"(v), "Nd"(p)); }

static uint32_t n_pci32(int bus, int dev, int fn, int off) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)fn << 8) | (uint32_t)(off & 0xFC);
    n_outl(0xCF8, addr);
    return n_inl(0xCFC);
}
static void n_pci32w(int bus, int dev, int fn, int off, uint32_t v) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)fn << 8) | (uint32_t)(off & 0xFC);
    n_outl(0xCF8, addr);
    n_outl(0xCFC, v);
}

static void nv_uart(const char* s) { for (; *s; s++) n_outb(0x3F8, (uint8_t)*s); }
static void nv_udec(int64_t n) {
    char b[24]; int i = 0; unsigned long long v;
    if (n < 0) { b[i++] = '-'; v = (unsigned long long)(-(n + 1)) + 1ull; } else v = (unsigned long long)n;
    char t[24]; int k = 0;
    do { t[k++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (k) b[i++] = t[--k];
    b[i] = 0; nv_uart(b);
}
static void nv_uhex(uint64_t v) {
    const char* h = "0123456789abcdef"; char o[19]; int n = 0; o[n++] = '0'; o[n++] = 'x';
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) { int d = (int)((v >> i) & 0xF); if (d || started || i == 0) { o[n++] = h[d]; started = 1; } }
    o[n] = 0; nv_uart(o);
}

/* --- регистры контроллера --- */
#define NV_CC      0x14
#define NV_CSTS    0x1C
#define NV_AQA     0x24
#define NV_ASQ     0x28
#define NV_ACQ     0x30
#define NV_CC_EN       (1u << 0)
#define NV_CC_IOSQES   (6u << 16)   /* 64-байтные команды */
#define NV_CC_IOCQES   (4u << 20)   /* 16-байтные завершения */
#define NV_CSTS_RDY    (1u << 0)

#define NV_ADMIN_IDENTIFY  0x06
#define NV_ADMIN_CREATE_SQ 0x01
#define NV_ADMIN_CREATE_CQ 0x05
#define NV_IO_WRITE        0x01
#define NV_IO_READ         0x02

#define NV_QENT 64

static volatile uint8_t* g_mmio = 0;
static volatile uint32_t* g_db = 0;      /* doorbells */
static uint32_t g_dstrd = 0;

/* Очереди в обычной RAM, но их пишет устройство (DMA), поэтому читать их
   надо как volatile: иначе компилятор выносит чтение завершения из цикла
   ожидания и «не видит» ответа никогда. */
static volatile uint32_t* g_asq = 0; static uint64_t g_asq_pa = 0;
static volatile uint32_t* g_acq = 0; static uint64_t g_acq_pa = 0;
static volatile uint32_t* g_isq = 0; static uint64_t g_isq_pa = 0;
static volatile uint32_t* g_icq = 0; static uint64_t g_icq_pa = 0;
static uint8_t*  g_buf = 0; static uint64_t g_buf_pa = 0;   /* identify + bounce */
static uint8_t*  g_bnc = 0; static uint64_t g_bnc_pa = 0;   /* bounce (4 КиБ) */

static uint32_t a_sq_tail = 0, a_cq_head = 0, a_cq_phase = 1;
static uint32_t i_sq_tail = 0, i_cq_head = 0, i_cq_phase = 1;
static uint32_t g_nsid = 1;
static uint64_t g_nsze = 0;        /* логических блоков NVMe */
static uint32_t g_lba_shift = 9;   /* log2(размер логического блока) */
static uint32_t g_cid = 1;
static int g_ok = 0;

static inline uint32_t nv_r32(uint32_t off) { return *(volatile uint32_t*)((uintptr_t)g_mmio + off); }
static inline void nv_w32(uint32_t off, uint32_t v) { *(volatile uint32_t*)((uintptr_t)g_mmio + off) = v; }

/* Общая выдача команды: sq/cq — очереди, doorbells — их индексы. */
static int nv_submit(volatile uint32_t* sq, volatile uint32_t* cq, uint32_t sq_db, uint32_t cq_db,
                     uint32_t* q_tail, uint32_t* q_head, uint32_t* q_phase,
                     const uint32_t* cmd, uint32_t* status_out) {
    for (int i = 0; i < 16; i++) sq[*q_tail * 16u + (uint32_t)i] = cmd[i];
    __asm__ __volatile__("" ::: "memory");
    *q_tail = (*q_tail + 1u) % NV_QENT;
    *(volatile uint32_t*)((uintptr_t)g_db + sq_db) = *q_tail;

    for (uint32_t spin = 0; spin < 20000000u; spin++) {
        /* QEMU отдаёт NVMe-завершения через bottom half: если гость крутится
           в плотном цикле без выходов из VM, BH не выполняется и завершение
           «не приходит». Периодическое MMIO-чтение заставляет VM выйти и даёт
           QEMU обработать очередь. На реальном железе это безвредно. */
        if ((spin & 0xFFFu) == 0xFFFu) (void)nv_r32(NV_CSTS);
        /* CQE.dw3 = Command Identifier (биты 15:0) | Status Field (31:16).
           Phase Tag — бит 0 статусного поля, т.е. бит 16 dw3 (не бит 0!). */
        uint32_t sf = cq[*q_head * 4u + 3u] >> 16;
        if ((sf & 1u) == *q_phase) {
            uint32_t st = (sf >> 1) & 0x7FFFu;
            if (status_out) *status_out = st;
            *q_head = (*q_head + 1u) % NV_QENT;
            if (*q_head == 0) *q_phase ^= 1u;
            *(volatile uint32_t*)((uintptr_t)g_db + cq_db) = *q_head;
            return (int)st;
        }
    }
    nv_uart("nvme: cq timeout dw3="); nv_uhex(cq[*q_head * 4u + 3u]);
    nv_uart(" head="); nv_udec((int64_t)*q_head); nv_uart(" ph="); nv_udec((int64_t)*q_phase);
    nv_uart("\n");
    return -1;
}

static int nv_admin(const uint32_t* cmd, uint32_t* status_out) {
    return nv_submit(g_asq, g_acq, 0, (4u << g_dstrd), &a_sq_tail, &a_cq_head, &a_cq_phase, cmd, status_out);
}

static int nv_io(uint64_t slba, uint16_t nlb, int is_read) {
    uint32_t cmd[16];
    for (int i = 0; i < 16; i++) cmd[i] = 0;
    cmd[0] = (uint32_t)(is_read ? NV_IO_READ : NV_IO_WRITE) | ((g_cid++) << 16);
    cmd[1] = g_nsid;
    cmd[6] = (uint32_t)g_bnc_pa;
    cmd[7] = (uint32_t)(g_bnc_pa >> 32);
    cmd[10] = (uint32_t)slba;
    cmd[11] = (uint32_t)(slba >> 32);
    cmd[12] = (uint32_t)(nlb - 1u) & 0xFFFFu;
    uint32_t db1 = (2u * (4u << g_dstrd));
    uint32_t db2 = (3u * (4u << g_dstrd));
    return nv_submit(g_isq, g_icq, db1, db2, &i_sq_tail, &i_cq_head, &i_cq_phase, cmd, 0);
}

int64_t k_nvme_sectors(void) {
    if (!g_ok) return 0;
    return (int64_t)(g_nsze << (g_lba_shift - 9u));
}
/* 512-байтные сектора ядра <-> логические блоки NVMe (512 или 4096). */
static int nv_rw(uint64_t lba512, uint16_t count512, void* buf, int is_write) {
    if (!g_ok || !count512) return -1;
    uint64_t total = (uint64_t)count512 * 512u;
    uint32_t blk = 512u << (g_lba_shift - 9u);
    uint64_t done = 0;
    while (done < total) {
        uint64_t cur = lba512 + (done >> 9);
        uint64_t nlba = cur >> (g_lba_shift - 9u);
        uint64_t off = (cur - (nlba << (g_lba_shift - 9u))) << 9;
        uint64_t chunk = blk - off;
        if (chunk > total - done) chunk = total - done;
        if (is_write) {
            if (chunk != blk) {                       /* частичный блок: RMW */
                if (nv_io(nlba, 1, 1) != 0) return -2;
            }
            for (uint64_t i = 0; i < chunk; i++) g_bnc[off + i] = ((uint8_t*)buf)[done + i];
            if (nv_io(nlba, 1, 0) != 0) return -3;
        } else {
            if (nv_io(nlba, 1, 1) != 0) return -2;
            for (uint64_t i = 0; i < chunk; i++) ((uint8_t*)buf)[done + i] = g_bnc[off + i];
        }
        done += chunk;
    }
    return 0;
}

int64_t k_nvme_read(uint64_t lba, uint16_t count, void* buf) {
    if (count > 8) return -1;
    return nv_rw(lba, count, buf, 0);
}
int64_t k_nvme_write(uint64_t lba, uint16_t count, const void* buf) {
    if (count > 8) return -1;
    return nv_rw(lba, count, (void*)buf, 1);
}

static int nv_find(int* obus, int* odev, int* ofn) {
    for (int bus = 0; bus < 8; bus++)
        for (int dev = 0; dev < 32; dev++)
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = n_pci32(bus, dev, fn, 0x00);
                if ((id & 0xFFFFu) == 0xFFFFu) continue;
                uint32_t cls = n_pci32(bus, dev, fn, 0x08);
                if (((cls >> 24) & 0xFF) == 0x01 && ((cls >> 16) & 0xFF) == 0x08 && ((cls >> 8) & 0xFF) == 0x02) {
                    *obus = bus; *odev = dev; *ofn = fn; return 1;
                }
            }
    return 0;
}

int64_t k_nvme_init(void) {
    int bus = 0, dev = 0, fn = 0;
    if (!nv_find(&bus, &dev, &fn)) return 0;
    uint32_t pcicmd = n_pci32(bus, dev, fn, 0x04);
    n_pci32w(bus, dev, fn, 0x04, pcicmd | 0x6u);
    uint32_t bar0 = n_pci32(bus, dev, fn, 0x10) & 0xFFFFFFF0u;
    uint32_t bar1 = n_pci32(bus, dev, fn, 0x14);
    uint64_t bar = (uint64_t)bar0 | ((uint64_t)bar1 << 32);
    nv_uart("nvme: pci "); nv_udec(bus); nv_uart(":"); nv_udec(dev); nv_uart("."); nv_udec(fn);
    nv_uart(" bar="); nv_uhex(bar); nv_uart("\n");
    if (!bar) return 0;
    g_mmio = (volatile uint8_t*)(uintptr_t)(bar + (uint64_t)k_kf_get_hhdm());

    uint64_t cap = *(volatile uint64_t*)((uintptr_t)g_mmio + 0x00);
    if ((cap & 0xFFFFu) == 0) return 0;
    g_dstrd = (uint32_t)((cap >> 32) & 0xFu);
    g_db = (volatile uint32_t*)((uintptr_t)g_mmio + 0x1000);

    /* сброс: CC.EN=0, ждём CSTS.RDY=0 */
    nv_w32(NV_CC, 0);
    for (int i = 0; i < 2000000 && (nv_r32(NV_CSTS) & NV_CSTS_RDY); i++) { }
    nv_w32(0x0C, 0xFFFFFFFFu);   /* INTMS: маскируем прерывания */

    int64_t asq = k_mem_palloc(), acq = k_mem_palloc();
    int64_t isq = k_mem_palloc(), icq = k_mem_palloc();
    int64_t buf = k_mem_palloc(), bnc = k_mem_palloc();
    if (!asq || !acq || !isq || !icq || !buf || !bnc) { nv_uart("nvme: alloc fail\n"); return 0; }
    g_asq = (volatile uint32_t*)(uintptr_t)asq; g_asq_pa = (uint64_t)k_mem_virt_to_phys(asq);
    g_acq = (volatile uint32_t*)(uintptr_t)acq; g_acq_pa = (uint64_t)k_mem_virt_to_phys(acq);
    g_isq = (volatile uint32_t*)(uintptr_t)isq; g_isq_pa = (uint64_t)k_mem_virt_to_phys(isq);
    g_icq = (volatile uint32_t*)(uintptr_t)icq; g_icq_pa = (uint64_t)k_mem_virt_to_phys(icq);
    g_buf = (uint8_t*)(uintptr_t)buf;  g_buf_pa = (uint64_t)k_mem_virt_to_phys(buf);
    g_bnc = (uint8_t*)(uintptr_t)bnc;  g_bnc_pa = (uint64_t)k_mem_virt_to_phys(bnc);

    /* Кадры из аллокатора — не нулевые. Очереди обязан видеть контроллер как
       пустые: мусор в CQ с «правильным» phase-битом ломает ожидание. */
    for (int i = 0; i < 1024; i++) {
        g_asq[i] = 0; g_acq[i] = 0; g_isq[i] = 0; g_icq[i] = 0;
    }

    /* admin-очереди */
    nv_w32(NV_AQA, (NV_QENT - 1u) | ((NV_QENT - 1u) << 16));
    *(volatile uint32_t*)((uintptr_t)g_mmio + NV_ASQ) = (uint32_t)g_asq_pa;
    *(volatile uint32_t*)((uintptr_t)g_mmio + NV_ASQ + 4) = (uint32_t)(g_asq_pa >> 32);
    *(volatile uint32_t*)((uintptr_t)g_mmio + NV_ACQ) = (uint32_t)g_acq_pa;
    *(volatile uint32_t*)((uintptr_t)g_mmio + NV_ACQ + 4) = (uint32_t)(g_acq_pa >> 32);

    nv_w32(NV_CC, NV_CC_EN | NV_CC_IOSQES | NV_CC_IOCQES);
    int ready = 0;
    for (int i = 0; i < 20000000; i++) { if (nv_r32(NV_CSTS) & NV_CSTS_RDY) { ready = 1; break; } }
    if (!ready) { nv_uart("nvme: controller not ready\n"); return 0; }

    /* IDENTIFY namespace 1 -> NSZE и формат LBA */
    uint32_t cmd[16];
    for (int i = 0; i < 16; i++) cmd[i] = 0;
    /* volatile: не даём компилятору развернуть это в векторные записи —
       SSE в ядре до инициализации FPU-состояния ловит #NM (в kf_ahci это
       уже стреляло ровно так же). */
    {
        volatile uint8_t* gb = (volatile uint8_t*)g_buf;
        for (int i = 0; i < 4096; i++) gb[i] = 0;
    }
    cmd[0] = NV_ADMIN_IDENTIFY | ((g_cid++) << 16);
    cmd[1] = 1;                       /* NSID = 1 */
    cmd[6] = (uint32_t)g_buf_pa;
    cmd[7] = (uint32_t)(g_buf_pa >> 32);
    cmd[10] = 0;                      /* CNS = 0: namespace */
    uint32_t st = 0;
    if (nv_admin(cmd, &st) != 0) { nv_uart("nvme: identify failed st="); nv_uhex(st); nv_uart("\n"); return 0; }
    g_nsze = *(volatile uint64_t*)(uintptr_t)g_buf;
    uint32_t flbas = g_buf[26] & 0x0Fu;
    uint32_t lbf = *(volatile uint32_t*)(uintptr_t)(g_buf + 128 + 4u * flbas);
    uint32_t lbads = (lbf >> 16) & 0xFFu;
    if (lbads >= 9u && lbads <= 12u) g_lba_shift = lbads;
    if (!g_nsze) { nv_uart("nvme: namespace empty\n"); return 0; }
    nv_uart("nvme: nsze="); nv_udec((int64_t)g_nsze); nv_uart(" lba="); nv_udec(1 << g_lba_shift); nv_uart("\n");

    /* I/O пара очередей (qid 1) */
    for (int i = 0; i < 16; i++) cmd[i] = 0;
    cmd[0] = NV_ADMIN_CREATE_CQ | ((g_cid++) << 16);
    cmd[10] = (uint32_t)((NV_QENT - 1u) << 16) | 1u;
    cmd[11] = 1u;                     /* PC=1 */
    cmd[6] = (uint32_t)g_icq_pa;
    cmd[7] = (uint32_t)(g_icq_pa >> 32);
    if (nv_admin(cmd, &st) != 0) { nv_uart("nvme: create cq failed st="); nv_uhex(st); nv_uart("\n"); return 0; }

    for (int i = 0; i < 16; i++) cmd[i] = 0;
    cmd[0] = NV_ADMIN_CREATE_SQ | ((g_cid++) << 16);
    cmd[10] = (uint32_t)((NV_QENT - 1u) << 16) | 1u;   /* QID=1, QSIZE=63 */
    cmd[11] = 1u | (1u << 16);        /* PC=1, CQID=1 (биты 31:16!) */
    cmd[6] = (uint32_t)g_isq_pa;
    cmd[7] = (uint32_t)(g_isq_pa >> 32);
    if (nv_admin(cmd, &st) != 0) { nv_uart("nvme: create sq failed st="); nv_uhex(st); nv_uart("\n"); return 0; }

    g_ok = 1;
    nv_uart("[KengaOS] NVME READY sec="); nv_udec(k_nvme_sectors()); nv_uart("\n");
    return 1;
}

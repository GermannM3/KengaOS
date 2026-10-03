/* kf_ahci.c — AHCI (SATA) диск для x86_64.
 *
 * Зачем: kf_disk.c говорит с диском через legacy ATA PIO (порты 0x1F0).
 * На современных машинах (и в QEMU -M q35) диска на этих портах нет —
 * контроллер работает в режиме AHCI, и без этого драйвера диск не виден,
 * а значит нет ни KengaFS, ни персистентности.
 *
 * AHCI — DMA-контроллер: command list (1 КиБ) + FIS receive (256 Б) +
 * command table с PRDT. Поллинг, без прерываний и без NCQ.
 * PCI — legacy config 0xCF8/0xCFC, class 0x01/subclass 0x06/prog-if 0x01,
 * ABAR = BAR5, MMIO — через HHDM (addr + hhdm). DMA-страницы —
 * k_mem_palloc(), физ. адрес — k_mem_virt_to_phys(). x86 когерентен по DMA.
 *
 * ВАЖНО про PRDT (иначе теряются все байты, кроме первого): порядок полей —
 *   QEMU hw/ide/ahci-internal.h: struct AHCI_SG { uint64_t addr;
 *                                                uint32_t reserved;
 *                                                uint32_t flags_size; }
 * т.е. DBC (байт-1) лежит в 12..15, а 8..11 — reserved. Если положить DBC
 * в 8..11, контроллер прочитает 0 и передаст ровно 1 байт.
 *
 * ponytail: один контроллер, один SATA-диск, слот 0 командного листа,
 * без NCQ/прерываний/hot-plug. Достаточно, чтобы KengaFS жила на реальном
 * SATA-диске и чтобы CI гонял -M q35.
 */
#include "kf_rt.h"

/* --- PCI config через порты 0xCF8/0xCFC --- */
static inline uint8_t  a_inb(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void     a_outb(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint32_t a_inl(uint16_t p) { uint32_t v; __asm__ __volatile__("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void     a_outl(uint16_t p, uint32_t v) { __asm__ __volatile__("outl %0,%1" : : "a"(v), "Nd"(p)); }

static uint32_t ah_pci32(int bus, int dev, int fn, int off) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)fn << 8) | (uint32_t)(off & 0xFC);
    a_outl(0xCF8, addr);
    return a_inl(0xCFC);
}
static void ah_pci32w(int bus, int dev, int fn, int off, uint32_t v) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)fn << 8) | (uint32_t)(off & 0xFC);
    a_outl(0xCF8, addr);
    a_outl(0xCFC, v);
}

/* --- журнал в UART (как в остальных драйверах ядра) --- */
static void ah_uart(const char* s) { for (; *s; s++) a_outb(0x3F8, (uint8_t)*s); }
static void ah_udec(int64_t n) {
    char b[24]; int i = 0; unsigned long long v;
    if (n < 0) { b[i++] = '-'; v = (unsigned long long)(-(n + 1)) + 1ull; }
    else v = (unsigned long long)n;
    char t[24]; int k = 0;
    do { t[k++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (k) b[i++] = t[--k];
    b[i] = 0; ah_uart(b);
}
static void ah_uhex(uint64_t v) {
    const char* h = "0123456789abcdef"; char o[19]; int n = 0; o[n++] = '0'; o[n++] = 'x';
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) { int d = (int)((v >> i) & 0xF); if (d || started || i == 0) { o[n++] = h[d]; started = 1; } }
    o[n] = 0; ah_uart(o);
}

/* --- регистры AHCI --- */
#define GHC_AE      (1u << 31)
#define PX_CLB      0x00
#define PX_CLBU     0x04
#define PX_FB       0x08
#define PX_FBU      0x0C
#define PX_IS       0x10
#define PX_IE       0x14
#define PX_CMD      0x18
#define PX_TFD      0x20
#define PX_SIG      0x24
#define PX_SSTS     0x28
#define PX_SERR     0x30
#define PX_CI       0x38
#define CMD_ST      (1u << 0)
#define CMD_FRE     (1u << 4)
#define CMD_CR      (1u << 15)
#define CMD_FR      (1u << 14)
#define TFD_ERR     (1u << 0)
#define TFD_BSY     (1u << 7)
#define TFD_DRQ     (1u << 3)
#define IS_TFES     (1u << 30)
#define SIG_SATA    0x00000101u
#define ATA_IDENTIFY      0xEC
#define ATA_READ_DMA_EXT  0x25
#define ATA_WRITE_DMA_EXT 0x35
#define AHCI_MAX_XFER 8u            /* секторов за команду (bounce = 4 КиБ) */

static volatile uint32_t* g_px = 0;
static volatile uint8_t*  g_clb = 0;    /* command list (VA) */
static uint64_t g_clb_pa = 0;
static volatile uint8_t*  g_fis = 0;    /* FIS receive (VA) */
static uint64_t g_fis_pa = 0;
static volatile uint8_t*  g_ct = 0;     /* command table (VA) */
static uint64_t g_ct_pa = 0;
static uint8_t*  g_bounce = 0;          /* DMA-буфер (VA) */
static uint64_t g_bounce_pa = 0;
static uint64_t g_sectors = 0;
static int g_port = -1;
static int g_ok = 0;

static inline uint32_t r32(volatile uint32_t* base, uint32_t off) { return base[off / 4]; }
static inline void w32(volatile uint32_t* base, uint32_t off, uint32_t v) { base[off / 4] = v; }
static inline void w64lo(volatile uint32_t* base, uint32_t off, uint64_t v) {
    base[off / 4] = (uint32_t)v;
    base[(off + 4) / 4] = (uint32_t)(v >> 32);
}

/* H2D register FIS + PRDT + выдача команды, поллинг завершения. */
static int ah_issue(uint8_t cmd, uint64_t lba, uint16_t count, int is_write) {
    volatile uint32_t* h = (volatile uint32_t*)(uintptr_t)g_clb;   /* слот 0: 32 байта */
    h[0] = 5u | (1u << 16);                     /* CFL=5 dwords, PRDTL=1 */
    if (is_write) h[0] |= (1u << 6);            /* W */
    h[1] = 0;                                   /* PRDBC = 0 */
    h[2] = (uint32_t)g_ct_pa;
    h[3] = (uint32_t)(g_ct_pa >> 32);

    volatile uint8_t* f = g_ct;                 /* CFIS[0x00..0x3F] */
    for (int i = 0; i < 64; i++) f[i] = 0;
    f[0] = 0x27;                                /* FIS type: H2D */
    f[1] = 0x80;                                /* C=1 */
    f[2] = cmd;
    f[4] = (uint8_t)(lba);
    f[5] = (uint8_t)(lba >> 8);
    f[6] = (uint8_t)(lba >> 16);
    f[7] = 0x40;                                /* LBA mode */
    f[8] = (uint8_t)(lba >> 24);
    f[9] = (uint8_t)(lba >> 32);
    f[10] = (uint8_t)(lba >> 40);
    f[12] = (uint8_t)(count);
    f[13] = (uint8_t)(count >> 8);

    /* PRDT: DBA(0..7), reserved(8..11), DBC+I(12..15) */
    volatile uint32_t* p = (volatile uint32_t*)(uintptr_t)(g_ct + 0x80);
    p[0] = (uint32_t)g_bounce_pa;
    p[1] = (uint32_t)(g_bounce_pa >> 32);
    p[2] = 0;
    p[3] = (uint32_t)count * 512u - 1u;         /* DBC = байт - 1 */

    for (int i = 0; i < 1000000; i++) {         /* ждём, пока порт свободен */
        uint32_t tfd = r32(g_px, PX_TFD);
        if (!(tfd & (TFD_BSY | TFD_DRQ))) break;
    }
    w32(g_px, PX_IS, 0xFFFFFFFFu);              /* сброс статусов */
    __asm__ __volatile__("" ::: "memory");
    w32(g_px, PX_CI, 1u);                       /* выдать слот 0 */

    int done = 0;
    for (int i = 0; i < 300000000; i++) {
        if (!(r32(g_px, PX_CI) & 1u)) { done = 1; break; }
    }
    uint32_t isr = r32(g_px, PX_IS);
    w32(g_px, PX_IS, 0xFFFFFFFFu);
    if (!done) return -1;
    if (isr & IS_TFES) return -2;
    if (r32(g_px, PX_TFD) & TFD_ERR) return -3;
    return 0;
}

int64_t k_ahci_sectors(void) { return g_ok ? (int64_t)g_sectors : 0; }
int64_t k_ahci_ready(void) { return g_ok; }

int64_t k_ahci_read(uint64_t lba, uint16_t count, void* buf) {
    if (!g_ok || !count || count > AHCI_MAX_XFER) return -1;
    int r = ah_issue(ATA_READ_DMA_EXT, lba, count, 0);
    if (r != 0) return r;
    uint8_t* d = (uint8_t*)buf;
    uint32_t n = (uint32_t)count * 512u;
    for (uint32_t i = 0; i < n; i++) d[i] = g_bounce[i];
    return 0;
}

int64_t k_ahci_write(uint64_t lba, uint16_t count, const void* buf) {
    if (!g_ok || !count || count > AHCI_MAX_XFER) return -1;
    const uint8_t* s = (const uint8_t*)buf;
    uint32_t n = (uint32_t)count * 512u;
    for (uint32_t i = 0; i < n; i++) g_bounce[i] = s[i];
    return ah_issue(ATA_WRITE_DMA_EXT, lba, count, 1);
}

/* Найти AHCI-контроллер: class 0x01 / subclass 0x06 / prog-if 0x01. */
static int ah_find(int* obus, int* odev, int* ofn) {
    for (int bus = 0; bus < 8; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = ah_pci32(bus, dev, fn, 0x00);
                if ((id & 0xFFFFu) == 0xFFFFu) continue;
                uint32_t cls = ah_pci32(bus, dev, fn, 0x08);
                if (((cls >> 24) & 0xFF) == 0x01 && ((cls >> 16) & 0xFF) == 0x06 && ((cls >> 8) & 0xFF) == 0x01) {
                    *obus = bus; *odev = dev; *ofn = fn;
                    return 1;
                }
            }
        }
    }
    return 0;
}

int64_t k_ahci_init(void) {
    int bus = 0, dev = 0, fn = 0;
    if (!ah_find(&bus, &dev, &fn)) return 0;

    /* memory space + bus master */
    uint32_t pcicmd = ah_pci32(bus, dev, fn, 0x04);
    ah_pci32w(bus, dev, fn, 0x04, pcicmd | 0x6u);
    uint32_t bar5 = ah_pci32(bus, dev, fn, 0x24) & 0xFFFFFFF0u;
    ah_uart("ahci: pci "); ah_udec(bus); ah_uart(":"); ah_udec(dev); ah_uart("."); ah_udec(fn);
    ah_uart(" bar="); ah_uhex(bar5); ah_uart("\n");
    if (!bar5) return 0;

    volatile uint32_t* abar = (volatile uint32_t*)(uintptr_t)(bar5 + (uint64_t)k_kf_get_hhdm());
    w32(abar, 0x04, r32(abar, 0x04) | GHC_AE);   /* режим AHCI */
    uint32_t pi = r32(abar, 0x0C);

    /* порт с SATA-диском: DET=3 (present), IPM=1 (active), SIG = SATA */
    for (int p = 0; p < 32; p++) {
        if (!((pi >> p) & 1u)) continue;
        volatile uint32_t* pr = (volatile uint32_t*)((uintptr_t)abar + 0x100 + 0x80 * (uint32_t)p);
        uint32_t ssts = r32(pr, PX_SSTS);
        if ((ssts & 0xFu) != 3u) continue;
        if (((ssts >> 8) & 0xFu) != 1u) continue;
        if (r32(pr, PX_SIG) != SIG_SATA) continue;
        g_port = p; g_px = pr; break;
    }
    if (g_port < 0) { ah_uart("ahci: no sata port\n"); return 0; }
    ah_uart("ahci: port "); ah_udec(g_port); ah_uart("\n");

    /* остановить движок порта */
    uint32_t pc = r32(g_px, PX_CMD);
    w32(g_px, PX_CMD, pc & ~(CMD_ST | CMD_FRE));
    for (int i = 0; i < 1000000 && (r32(g_px, PX_CMD) & (CMD_CR | CMD_FR)); i++) { }

    /* страницы: admin (CLB + FB + CT) и bounce */
    int64_t admin_v = k_mem_palloc();
    int64_t bounce_v = k_mem_palloc();
    if (!admin_v || !bounce_v) { ah_uart("ahci: alloc fail\n"); return 0; }
    uint64_t admin_pa = (uint64_t)k_mem_virt_to_phys(admin_v);
    g_bounce_pa = (uint64_t)k_mem_virt_to_phys(bounce_v);
    g_bounce = (uint8_t*)(uintptr_t)bounce_v;
    volatile uint8_t* adm = (volatile uint8_t*)(uintptr_t)admin_v;
    for (int i = 0; i < 4096; i++) adm[i] = 0;
    volatile uint8_t* gbv = (volatile uint8_t*)(uintptr_t)bounce_v;
    for (int i = 0; i < 4096; i++) gbv[i] = 0;
    g_clb = adm;                    /* 1024-байтное выравнивание */
    g_clb_pa = admin_pa;
    g_fis = adm + 0x400;            /* 256-байтное выравнивание */
    g_fis_pa = admin_pa + 0x400;
    g_ct = adm + 0x500;             /* 128-байтное выравнивание */
    g_ct_pa = admin_pa + 0x500;

    w64lo(g_px, PX_CLB, g_clb_pa);
    w64lo(g_px, PX_FB, g_fis_pa);
    w32(g_px, PX_IS, 0xFFFFFFFFu);
    w32(g_px, PX_SERR, 0xFFFFFFFFu);
    w32(g_px, PX_IE, 0);            /* поллинг */
    w32(g_px, PX_CMD, r32(g_px, PX_CMD) | CMD_FRE);
    w32(g_px, PX_CMD, r32(g_px, PX_CMD) | CMD_ST);
    for (int i = 0; i < 1000000 && !(r32(g_px, PX_CMD) & CMD_CR); i++) { }
    if (!(r32(g_px, PX_CMD) & CMD_CR)) { ah_uart("ahci: engine not running\n"); return 0; }

    /* IDENTIFY DEVICE -> число секторов */
    g_ok = 1;
    if (ah_issue(ATA_IDENTIFY, 0, 1, 0) != 0) { g_ok = 0; ah_uart("ahci: identify failed\n"); return 0; }
    uint16_t* id = (uint16_t*)g_bounce;
    uint64_t lbah = ((uint64_t)id[103] << 48) | ((uint64_t)id[102] << 32)
                  | ((uint64_t)id[101] << 16) | (uint64_t)id[100];
    g_sectors = lbah ? lbah : (((uint64_t)id[61] << 16) | (uint64_t)id[60]);
    if (!g_sectors) { g_ok = 0; ah_uart("ahci: zero sectors\n"); return 0; }

    ah_uart("[KengaOS] AHCI READY sec="); ah_udec((int64_t)g_sectors); ah_uart("\n");
    return 1;
}

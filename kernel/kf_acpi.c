/* kf_acpi.c — ACPI: мягкое выключение и перезагрузка (x86_64).
 *
 * Без ACPI «poweroff» — это QEMU-хак (порт 0xf4) или кнопка питания.
 * Здесь: находим RSDP (скан 0xE0000..0xFFFFF), идём в RSDT/XSDT -> FADT,
 * достаём PM1a/PM1b_CNT_BLK, разбираем \_S5 из DSDT (SLP_TYPa/b) и пишем
 * SLP_EN. Перезагрузка — через FADT RESET_REG, если прошивка его дала.
 *
 * Все таблицы читаются через HHDM (pa + hhdm), поэтому ничего не мапим.
 */
#include "kf_rt.h"

static inline uint8_t  a_inb(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void     a_outb(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline void     a_outw(uint16_t p, uint16_t v) { __asm__ __volatile__("outw %0,%1" : : "a"(v), "Nd"(p)); }

static void ac_uart(const char* s) { for (; *s; s++) a_outb(0x3F8, (uint8_t)*s); }
static void ac_udec(int64_t n) {
    char b[24]; int i = 0; unsigned long long v;
    if (n < 0) { b[i++] = '-'; v = (unsigned long long)(-(n + 1)) + 1ull; } else v = (unsigned long long)n;
    char t[24]; int k = 0;
    do { t[k++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (k) b[i++] = t[--k];
    b[i] = 0; ac_uart(b);
}
static void ac_uhex(uint64_t v) {
    const char* h = "0123456789abcdef"; char o[19]; int n = 0; o[n++] = '0'; o[n++] = 'x';
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) { int d = (int)((v >> i) & 0xF); if (d || started || i == 0) { o[n++] = h[d]; started = 1; } }
    o[n] = 0; ac_uart(o);
}

static uint64_t g_hhdm = 0;
static uint16_t g_pm1a_cnt = 0, g_pm1b_cnt = 0;
static uint32_t g_slp_a = 5, g_slp_b = 5;   /* типовое S5, если \_S5 не разобрался */
static uint64_t g_reset_addr = 0;
static uint8_t  g_reset_val = 0, g_reset_space = 0xFF;
static int g_ready = 0;

static volatile uint8_t* ac_phys(uint64_t pa) { return (volatile uint8_t*)(uintptr_t)(pa + g_hhdm); }
static uint32_t ac_rd32(uint64_t pa) {
    volatile uint8_t* p = ac_phys(pa);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int ac_sig(uint64_t pa, const char* s) {
    volatile uint8_t* p = ac_phys(pa);
    for (int i = 0; i < 4; i++) if (p[i] != (uint8_t)s[i]) return 0;
    return 1;
}

/* RSDP: "RSD PTR " на 16-байтной границе.
 * 1) По карте памяти от загрузчика ищем в областях ACPI-reclaimable (тип 2) и
 *    ACPI-NVS (тип 3) — это работает и под UEFI, где legacy-области нет.
 *    Запрос memmap у Limine старый и отвечает даже при базовой ревизии 0.
 * 2) Затем legacy-скан 0xE0000..0xFFFFF (только BIOS).
 */
extern volatile uint64_t limine_memmap_request[];

static int ac_rsdp_at(uint64_t pa) {
    if (!pa) return 0;
    if (!ac_sig(pa, "RSD ")) return 0;
    volatile uint8_t* p = ac_phys(pa);
    return p[4]=='P'&&p[5]=='T'&&p[6]=='R'&&p[7]==' ';
}

static uint64_t ac_find_rsdp_memmap(void) {
    uint64_t resp = limine_memmap_request[5];
    if (!resp) return 0;
    /* struct limine_memmap_response { revision; entry_count; entries** } */
    volatile uint64_t* rr = (volatile uint64_t*)(uintptr_t)resp;
    uint64_t cnt = rr[1];
    uint64_t ents = rr[2];
    if (!cnt || cnt > 256 || !ents) return 0;
    for (uint64_t i = 0; i < cnt; i++) {
        uint64_t e = *(volatile uint64_t*)(uintptr_t)(ents + i * 8);
        if (!e) continue;
        volatile uint64_t* ep = (volatile uint64_t*)(uintptr_t)e;
        uint64_t base = ep[0], len = ep[1], type = ep[2];
        if (type != 2 && type != 3) continue;          /* ACPI reclaimable / NVS */
        if (len > (1ull << 26)) len = 1ull << 26;
        for (uint64_t off = 0; off + 16 <= len; off += 16) {
            if (ac_rsdp_at(base + off)) return base + off;
        }
    }
    return 0;
}

static uint64_t ac_find_rsdp(void) {
    uint64_t m = ac_find_rsdp_memmap();
    if (m) { ac_uart("acpi: rsdp via memmap\n"); return m; }
    for (uint64_t pa = 0xE0000; pa < 0x100000; pa += 16) {
        if (ac_sig(pa, "RSD ")) {
            volatile uint8_t* p = ac_phys(pa);
            if (p[4]=='P'&&p[5]=='T'&&p[6]=='R'&&p[7]==' ') return pa;
        }
    }
    return 0;
}

/* таблица по сигнатуре: RSDT (32-бит) или XSDT (64-бит) */
static uint64_t ac_find_table(uint64_t rsdp, const char* sig) {
    uint8_t rev = ac_phys(rsdp)[15];
    uint64_t root = 0; int wide = 0;
    if (rev >= 2) {
        root = (uint64_t)ac_rd32(rsdp + 24) | ((uint64_t)ac_rd32(rsdp + 28) << 32);
        if (root) wide = 1;
    }
    if (!root) root = ac_rd32(rsdp + 16);
    if (!root) return 0;
    uint32_t len = ac_rd32(root + 4);
    if (len < 36 || len > 4096) return 0;
    uint32_t n = (len - 36) / (wide ? 8u : 4u);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t t;
        if (wide) t = (uint64_t)ac_rd32(root + 36 + i * 8) | ((uint64_t)ac_rd32(root + 40 + i * 8) << 32);
        else t = ac_rd32(root + 36 + i * 4);
        if (t && ac_sig(t, sig)) return t;
    }
    return 0;
}

/* \_S5 из DSDT: Name(_S5_, Package(2){ 0x0A a, 0x0A b }) */
static int ac_find_s5(uint64_t dsdt, uint32_t* a, uint32_t* b) {
    volatile uint8_t* p = ac_phys(dsdt);
    uint32_t len = ac_rd32(dsdt + 4);
    if (len < 36 || len > (1u << 20)) return 0;
    for (uint32_t i = 0; i + 8 < len; i++) {
        if (p[i] == '_' && p[i+1] == 'S' && p[i+2] == '5' && p[i+3] == '_') {
            for (uint32_t j = i + 4; j + 3 < len && j < i + 48; j++) {
                if (p[j] != 0x12) continue;          /* PackageOp */
                uint32_t k = j + 1;
                uint8_t lead = p[k];
                if ((lead & 0xC0) == 0) k += 1;
                else if ((lead & 0xC0) == 0x40) k += 2;
                else if ((lead & 0xC0) == 0x80) k += 3;
                else k += 4;
                if (k + 1 >= len) break;
                k += 1;                              /* NumElements */
                uint32_t v[2] = { 0, 0 };
                for (int e = 0; e < 2; e++) {
                    if (k >= len) break;
                    uint8_t op = p[k++];
                    if (op == 0x0A) { v[e] = p[k++]; }
                    else if (op == 0x00) { v[e] = 0; }
                    else if (op == 0x01) { v[e] = 1; }
                    else break;
                }
                *a = v[0]; *b = v[1];
                return 1;
            }
        }
    }
    return 0;
}

int64_t k_acpi_ready(void) { return g_ready; }

int64_t k_acpi_init(void) {
    g_hhdm = (uint64_t)k_kf_get_hhdm();
    uint64_t rsdp = ac_find_rsdp();
    if (!rsdp) { ac_uart("[KengaOS] ACPI NONE\n"); return 0; }
    uint64_t fadt = ac_find_table(rsdp, "FACP");
    if (!fadt) { ac_uart("[KengaOS] ACPI NOFADT\n"); return 0; }

    uint32_t flen = ac_rd32(fadt + 4);
    g_pm1a_cnt = (uint16_t)ac_rd32(fadt + 64);
    g_pm1b_cnt = (uint16_t)ac_rd32(fadt + 68);
    uint64_t dsdt = ac_rd32(fadt + 40);
    ac_uart("acpi: fadt="); ac_uhex(fadt); ac_uart(" len="); ac_udec((int64_t)flen);
    ac_uart(" dsdt="); ac_uhex(dsdt); ac_uart("\n");
    uint32_t sa = 0, sb = 0;
    if (dsdt && ac_find_s5(dsdt, &sa, &sb)) { g_slp_a = sa; g_slp_b = sb; }

    if (flen >= 129) {   /* RESET_REG появился только в FADT rev>=2 */
        g_reset_space = ac_phys(fadt + 124)[0];
        g_reset_addr = (uint64_t)ac_rd32(fadt + 128) | ((uint64_t)ac_rd32(fadt + 132) << 32);
        g_reset_val = ac_phys(fadt + 136)[0];
    }

    g_ready = 1;
    ac_uart("[KengaOS] ACPI READY pm1a="); ac_uhex(g_pm1a_cnt);
    ac_uart(" s5="); ac_udec((int64_t)g_slp_a);
    if (g_reset_addr) { ac_uart(" reset="); ac_uhex(g_reset_addr); }
    ac_uart("\n");
    return 1;
}

int64_t k_acpi_shutdown(void) {
    if (!g_ready || !g_pm1a_cnt) return 0;
    uint16_t en = (uint16_t)(1u << 13);                     /* SLP_EN */
    ac_uart("[KengaOS] ACPI POWER OFF\n");
    a_outw(g_pm1a_cnt, (uint16_t)((g_slp_a << 10) | en));
    if (g_pm1b_cnt) a_outw(g_pm1b_cnt, (uint16_t)((g_slp_b << 10) | en));
    return 1;
}

int64_t k_acpi_reboot(void) {
    if (!g_ready || !g_reset_addr) return 0;
    if (g_reset_space == 1) { a_outb((uint16_t)g_reset_addr, g_reset_val); return 1; }
    if (g_reset_space == 0) { *(volatile uint8_t*)(uintptr_t)(g_reset_addr + g_hhdm) = g_reset_val; return 1; }
    return 0;
}

/* virtio_a64.c — virtio-blk (legacy, virtio-mmio) для QEMU virt.
   Единая раскладка регистров (include/standard-headers/linux/virtio_mmio.h):
   QSEL 0x30, NUM_MAX 0x34, NUM 0x38, ALIGN 0x3c, PFN 0x40, NOTIFY 0x50,
   ISR 0x60, STATUS 0x70, CFG 0x100. Одна vq на transport, поллинг по ISR.
   Кэш: чистим страницы перед notify, инвалидируем после completion —
   устройство работает с RAM напрямую (DMA).
   DMA-страницы только выше 0x42000000 (внизу образ ядра). */
#include "kf_rt.h"

#define VMMIO_BASE  0x0a000000ull
#define VMMIO_STEP  0x200ull
#define VMMIO_SLOTS 32
#define VBLK_MAX    4

#define R_MAGIC   0x000
#define R_VERSION 0x004
#define R_DEVID   0x008
#define R_GF      0x020
#define R_GPAGE   0x028
#define R_QSEL    0x030
#define R_QNUMMAX 0x034
#define R_QNUM    0x038
#define R_QALIGN  0x03C
#define R_QPFN    0x040
#define R_QNOTIFY 0x050
#define R_INTST   0x060
#define R_INTACK  0x064
#define R_STATUS  0x070
#define R_CFG     0x100

#define VSTAT_ACK 1
#define VSTAT_DRV 2
#define VSTAT_OK  4

#define VR_N      8
#define VR_HDR    0x100
#define VR_DATA   0x200
#define VR_STAT   0x400

typedef struct {
    volatile uint32_t* base;   /* MMIO база транспорта */
    uint64_t va;               /* hhdm VA vring (2 страницы) */
    uint64_t pa;               /* физический адрес vring */
    uint64_t cap;              /* capacity в секторах */
    uint32_t last_used;        /* последний обработанный used.idx */
} vblk_t;

static vblk_t vblk[VBLK_MAX];
static int     vblk_n = 0;

static inline uint32_t r32(volatile uint32_t* b, int off) { return b[off / 4]; }
static inline void     w32(volatile uint32_t* b, int off, uint32_t v) { b[off / 4] = v; }
static void dmb(void) { __asm__ __volatile__("dmb ish" ::: "memory"); }

/* кэш-обслуживание: устройство работает с RAM напрямую (DMA),
   CPU-кэш надо чистить/инвалидировать вручную */
static void cache_op(uint64_t va, uint64_t len, int invalidate) {
    for (uint64_t a = va & ~63ull; a < va + len; a += 64) {
        if (invalidate) __asm__ __volatile__("dc ivac, %0" :: "r"(a) : "memory");
        else            __asm__ __volatile__("dc cvac, %0" :: "r"(a) : "memory");
    }
    __asm__ __volatile__("dsb ish" ::: "memory");
}

static int vring_setup(vblk_t* d) {
    /* 2 последовательные страницы: palloc first-fit после скипа */
    for (int t = 0; t < 16; t++) {
        int64_t p1 = k_mem_palloc();
        int64_t p2 = k_mem_palloc();
        if (!p1 || !p2) { if (p1) k_mem_pfree(p1); if (p2) k_mem_pfree(p2); return 0; }
        if (p2 == p1 + 4096) {
            d->va = (uint64_t)p1;
            d->pa = (uint64_t)k_mem_virt_to_phys(p1);
            volatile uint8_t* m = (volatile uint8_t*)p1;
            for (int i = 0; i < 8192; i++) m[i] = 0;
            w32(d->base, R_QSEL, 0);
            w32(d->base, R_QNUM, VR_N);
            w32(d->base, R_QALIGN, 4096);
            w32(d->base, R_QPFN, (uint32_t)(d->pa >> 12));   /* активирует очередь */
            d->last_used = 0;
            return 1;
        }
        k_mem_pfree(p1); k_mem_pfree(p2);
    }
    return 0;
}

int k_vblk_init(void) {
    vblk_n = 0;
    /* съесть низкие кадры: DMA только выше 0x42000000 (там образ ядра).
       ponytail: кадры ниже порога занимаются навсегда (~32 МБ) — дешевле,
       чем скип-лист в kf_mem.c; апгрейд — там */
    for (;;) {
        int64_t p = k_mem_palloc();
        if (!p) break;
        if ((uint64_t)k_mem_virt_to_phys(p) >= 0x42000000ull) break;  /* p остаётся занят */
    }
    for (int s = 0; s < VMMIO_SLOTS && vblk_n < VBLK_MAX; s++) {
        volatile uint32_t* b = (volatile uint32_t*)(uintptr_t)(VMMIO_BASE + s * VMMIO_STEP);
        if (r32(b, R_MAGIC) != 0x74726976u) continue;      /* "virt" */
        if (r32(b, R_VERSION) != 1) continue;               /* legacy */
        if (r32(b, R_DEVID) != 2) continue;                 /* block */

        vblk_t* d = &vblk[vblk_n];
        d->base = b;
        w32(b, R_STATUS, 0);                                /* reset */
        w32(b, R_STATUS, VSTAT_ACK);
        w32(b, R_STATUS, VSTAT_ACK | VSTAT_DRV);
        w32(b, R_GF, 0);                                    /* legacy blk: фичи не нужны */
        w32(b, R_GPAGE, 4096);
        if (!vring_setup(d)) continue;
        d->cap = (uint64_t)r32(b, R_CFG) | ((uint64_t)r32(b, R_CFG + 4) << 32);
        w32(b, R_STATUS, VSTAT_ACK | VSTAT_DRV | VSTAT_OK);
        vblk_n++;
    }
    return vblk_n;
}

int k_vblk_count(void) { return vblk_n; }
uint64_t k_vblk_sectors(int idx) { return (idx >= 0 && idx < vblk_n) ? vblk[idx].cap : 0; }

/* одна операция 512Б: type 0 = READ (device пишет нам), 1 = WRITE */
static int vblk_xfer(vblk_t* d, uint64_t sector, void* buf, int is_write) {
    volatile uint8_t* m = (volatile uint8_t*)d->va;
    volatile uint32_t* hdr = (volatile uint32_t*)(m + VR_HDR);
    hdr[0] = is_write ? 1u : 0u;
    hdr[1] = 0;
    *(volatile uint64_t*)(m + VR_HDR + 8) = sector;
    if (is_write) {
        for (int i = 0; i < 512; i++) m[VR_DATA + i] = ((uint8_t*)buf)[i];
    }
    m[VR_STAT] = 0xEE;

    /* desc: u64 addr, u32 len, u16 flags, u16 next; 1=NEXT, 2=WRITE(dev) */
    volatile struct { uint64_t a; uint32_t l; uint16_t f; uint16_t n; }* D =
        (volatile void*)(m);
    D[0].a = d->pa + VR_HDR; D[0].l = 16; D[0].f = 1; D[0].n = 1;
    D[1].a = d->pa + VR_DATA; D[1].l = 512; D[1].f = 1 | (is_write ? 0 : 2); D[1].n = 2;
    D[2].a = d->pa + VR_STAT; D[2].l = 1; D[2].f = 2; D[2].n = 0;

    volatile uint16_t* avail = (volatile uint16_t*)(m + 16 * VR_N);
    uint16_t ai = avail[1];
    avail[2 + (ai % VR_N)] = 0;             /* head desc */
    dmb();
    avail[1] = (uint16_t)(ai + 1);
    cache_op(d->va, 4096, 0);               /* desc+avail+hdr+data+status → RAM */
    dmb();
    w32(d->base, R_QNOTIFY, 0);

    /* поллинг по ISR (MMIO, некэшируется); RAM-кольца инвалидируем после */
    volatile uint16_t* used_idx = (volatile uint16_t*)(m + 4096 + 2);
    for (volatile int t = 0; t < 20000000; t++) {
        if (r32(d->base, R_INTST) & 1) {
            cache_op(d->va + 4096, 4096, 1);
            cache_op(d->va + VR_STAT, 8, 1);
            if (!is_write) cache_op(d->va + VR_DATA, 512, 1);
            dmb();
            uint16_t unow = *used_idx;
            w32(d->base, R_INTACK, 1);
            if (unow != d->last_used) {
                d->last_used = unow;
                uint8_t st = m[VR_STAT];
                if (!is_write && st == 0) {
                    for (int i = 0; i < 512; i++) ((uint8_t*)buf)[i] = m[VR_DATA + i];
                }
                return st == 0 ? 0 : -1;
            }
        }
    }
    return -2;   /* таймаут */
}

int k_vblk_read(int idx, uint64_t lba, uint16_t count, void* buf) {
    if (idx < 0 || idx >= vblk_n) return -1;
    uint8_t* out = (uint8_t*)buf;
    for (uint16_t i = 0; i < count; i++) {
        int r = vblk_xfer(&vblk[idx], lba + i, out + i * 512, 0);
        if (r) return r;
    }
    return 0;
}

int k_vblk_write(int idx, uint64_t lba, uint16_t count, const void* buf) {
    if (idx < 0 || idx >= vblk_n) return -1;
    const uint8_t* in = (const uint8_t*)buf;
    for (uint16_t i = 0; i < count; i++) {
        int r = vblk_xfer(&vblk[idx], lba + i, (void*)(in + i * 512), 1);
        if (r) return r;
    }
    return 0;
}

/* индекс устройства с маркером KENGARWTEST1 на LBA0 */
int k_vblk_find_marker(void) {
    uint8_t sec[512];
    for (int i = 0; i < vblk_n; i++) {
        if (k_vblk_read(i, 0, 1, sec)) continue;
        const char* mk = "KENGARWTEST1";
        int ok = 1;
        for (int j = 0; j < 12; j++) if (sec[j] != (uint8_t)mk[j]) { ok = 0; break; }
        if (ok) return i;
    }
    return -1;
}

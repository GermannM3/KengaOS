/* kf_mem.c — physical memory, kernel heap and memory map info.
 *
 * Parses the Limine memory map: picks the largest USABLE region (capped to
 * 16 MiB) as the kernel heap arena, and gives every other usable frame to a
 * bitmap-based 4 KiB frame allocator. The memory map itself is recorded for
 * the `mmap` shell command.
 */
#include "kf_rt.h"

#define PAGE_SIZE 4096ULL

static uint64_t hhdm = 0;

static uint64_t heap_base = 0;   /* HHDM-mapped virtual base */
static uint64_t heap_size = 0;
static uint64_t heap_used = 0;

static uint64_t kf_mem_total = 0;

/* --- memory map regions (for `mmap` command) --- */
#define MEM_REGIONS_MAX 64
static uint64_t mm_base[MEM_REGIONS_MAX];
static uint64_t mm_len[MEM_REGIONS_MAX];
static uint64_t mm_type[MEM_REGIONS_MAX];
static int      mm_count = 0;

static void record_region(uint64_t base, uint64_t len, uint64_t type) {
    if (mm_count < MEM_REGIONS_MAX) {
        mm_base[mm_count] = base;
        mm_len[mm_count] = len;
        mm_type[mm_count] = type;
        mm_count++;
    }
}

int64_t k_mem_region_count(void) { return mm_count; }
int64_t k_mem_region_base(int64_t i) { return (i >= 0 && i < mm_count) ? (int64_t)mm_base[i] : 0; }
int64_t k_mem_region_len(int64_t i) { return (i >= 0 && i < mm_count) ? (int64_t)mm_len[i] : 0; }
int64_t k_mem_region_type(int64_t i) { return (i >= 0 && i < mm_count) ? (int64_t)mm_type[i] : -1; }

/* --- physical frame allocator (4 KiB bitmap, safe) --- */
#define FRAME_MAX    (512u * 1024 * 1024)
#define FRAME_BITS   (FRAME_MAX / PAGE_SIZE)
#define FRAME_BYTES  ((FRAME_BITS + 7) / 8)
static uint8_t  fbmap[FRAME_BYTES];
/* A free bit alone cannot distinguish a reserved frame from a frame that was
   allocated and then freed. Keep ownership metadata so k_mem_pfree cannot
   accidentally release firmware/kernel/framebuffer memory. */
static uint8_t  managedmap[FRAME_BYTES];
static uint64_t free_frames = 0;

static inline void bmap_set(uint64_t i) { fbmap[i >> 3] |= (uint8_t)(1u << (i & 7)); }
static inline void bmap_clr(uint64_t i) { fbmap[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
static inline int  bmap_get(uint64_t i) { return (fbmap[i >> 3] >> (i & 7)) & 1; }

static void frame_add_range(uint64_t phys, uint64_t len) {
    uint64_t start = (phys + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t end = (phys + len) & ~(PAGE_SIZE - 1);
    for (uint64_t p = start; p + PAGE_SIZE <= end; p += PAGE_SIZE) {
        if (p < 0x100000ULL || p >= FRAME_MAX) continue;
        bmap_set(p / PAGE_SIZE);
        managedmap[(p / PAGE_SIZE) >> 3] |= (uint8_t)(1u << ((p / PAGE_SIZE) & 7));
        free_frames++;
    }
}

int64_t k_mem_init(void) {
    hhdm = (uint64_t)k_kf_get_hhdm();
    int64_t resp = k_kf_get_memmap();
    if (!resp) return 0;

    uint64_t count = *(uint64_t*)(resp + 8);
    uint64_t** entries = *(uint64_t***)(resp + 16);   /* array of pointers */

    uint64_t best_base = 0, best_len = 0;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t* e = entries[i];          /* base@0, length@8, type@16 */
        uint64_t base = e[0], len = e[1], type = e[2];
        record_region(base, len, type);
        if (type != 0) continue;           /* usable only */
        kf_mem_total += len;
        if (base < 0x100000ULL) continue;
        if (len > best_len) { best_len = len; best_base = base; }
    }
    if (best_len == 0) return 0;

    uint64_t start = (best_base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t end = (best_base + best_len) & ~(PAGE_SIZE - 1);
    if (start >= end) return 0;

    /* Heap arena: capped to 16 MiB; the rest goes to the frame free-list. */
    const uint64_t HEAP_MAX = 16u * 1024 * 1024;
    uint64_t heap_end = start + (end - start > HEAP_MAX ? HEAP_MAX : (end - start));
    heap_end &= ~(PAGE_SIZE - 1);

    heap_base = start + hhdm;
    heap_size = heap_end - start;
    heap_used = 0;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t* e = entries[i];
        uint64_t base = e[0], len = e[1], type = e[2];
        if (type != 0) continue;
        if (base < 0x100000ULL) continue;
        if (base >= start && base + len <= heap_end) continue;
        uint64_t r0 = base, r1 = base + len;
        if (r0 < start) frame_add_range(r0, start - r0);
        if (r1 > heap_end) {
            uint64_t s2 = (r0 > heap_end) ? r0 : heap_end;   /* only the region's own tail */
            if (r1 > s2) frame_add_range(s2, r1 - s2);
        }
    }
    return 1;
}

int64_t k_mem_free_bytes(void) { return (int64_t)(heap_size - heap_used); }
int64_t k_mem_total_bytes(void) { return (int64_t)kf_mem_total; }

int64_t k_mem_palloc(void) {
    for (uint64_t i = 0; i < FRAME_BITS; i++) {
        if (bmap_get(i)) {
            bmap_clr(i);
            free_frames--;
            return (int64_t)(uintptr_t)(i * PAGE_SIZE + hhdm);
        }
    }
    return 0;
}

int64_t k_mem_pfree(int64_t addr) {
    if ((uint64_t)addr < hhdm) return 0;
    uint64_t phys = (uint64_t)addr - hhdm;
    if (phys % PAGE_SIZE || phys >= FRAME_MAX) return 0;
    uint64_t bit = phys / PAGE_SIZE;
    if (!(managedmap[bit >> 3] & (uint8_t)(1u << (bit & 7)))) return 0;
    if (bmap_get(bit)) return 0;
    bmap_set(bit);
    free_frames++;
    return 1;
}

int64_t k_mem_pages_free(void) { return (int64_t)free_frames; }

/* Convert a kernel virtual (HHDM-mapped) address back to a physical one.
   Needed by DMA-capable drivers (UHCI frame lists live in physical memory). */
int64_t k_mem_virt_to_phys(int64_t addr) {
    if ((uint64_t)addr < hhdm) return 0;
    return (int64_t)((uint64_t)addr - hhdm);
}

/* Fallback arena used until mem_init() sets up the real heap (the runtime
   calls kf_alloc very early, before mem_init runs). */
static uint8_t  fallback_arena[8192];
static uint64_t fallback_used = 0;

/* Runtime allocator hook (strong symbol). Page-aligned bump over the arena. */
void* kf_alloc(size_t n) {
    if (n == 0) n = 1;
    n = (n + 15u) & ~(size_t)15u;
    if (heap_size > 0) {
        if (heap_used + n > heap_size) return 0;  /* OOM */
        void* p = (void*)(heap_base + heap_used);
        heap_used += n;
        return p;
    }
    if (fallback_used + n > sizeof(fallback_arena)) return 0;
    void* p = &fallback_arena[fallback_used];
    fallback_used += n;
    return p;
}

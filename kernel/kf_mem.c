/* kf_mem.c — physical memory + kernel heap (M2.4).
 *
 * Parses the Limine memory map, picks the largest USABLE region (skipping the
 * kernel image), maps it into the higher half via the HHDM offset, and uses it
 * as the kernel heap arena. kf_alloc() (the runtime's weak hook) now allocates
 * from this arena instead of a tiny static buffer, so the Kenga runtime gets a
 * real heap. Allocation is a page-aligned bump for now; buddy comes later.
 */
#include "kf_rt.h"

#define PAGE_SIZE 4096ULL

static uint64_t hhdm = 0;

static uint64_t heap_base = 0;   /* HHDM-mapped virtual base */
static uint64_t heap_size = 0;
static uint64_t heap_used = 0;
static uint64_t heap_phys_base = 0;

static uint64_t kf_mem_total = 0;

/* --- physical frame allocator (4 KiB bitmap, safe) ---
   One bit per frame in a static array (no writes into the frames themselves).
   Range capped at FRAME_MAX; built from usable regions except the heap arena. */
#define FRAME_MAX    (512u * 1024 * 1024)
#define FRAME_BITS   (FRAME_MAX / PAGE_SIZE)
#define FRAME_BYTES  ((FRAME_BITS + 7) / 8)
static uint8_t  fbmap[FRAME_BYTES];
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
        free_frames++;
    }
}

int64_t k_mem_init(void) {
    hhdm = (uint64_t)k_kf_get_hhdm();
    int64_t resp = k_kf_get_memmap();
    if (!resp) return 0;

    uint64_t count = *(uint64_t*)(resp + 8);
    uint64_t** entries = *(uint64_t***)(resp + 16);   /* array of pointers */


    /* Pick the largest USABLE region. Limine marks the kernel image as
       reserved, so usable regions don't overlap it. We start the heap above
       1 MiB to stay clear of low memory / the null page. */
    uint64_t best_base = 0, best_len = 0;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t* e = entries[i];          /* base@0, length@8, type@16 */
        uint64_t base = e[0], len = e[1], type = e[2];
        if (type != 0) continue;           /* usable only */
        kf_mem_total += len;
        if (base < 0x100000ULL) continue;  /* skip low memory */
        if (len > best_len) { best_len = len; best_base = base; }
    }

    if (best_len == 0) return 0;
    /* page-align base up, length down */
    uint64_t start = (best_base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t end = (best_base + best_len) & ~(PAGE_SIZE - 1);
    if (start >= end) return 0;

    /* Heap arena: capped to a modest contiguous chunk; the rest of the largest
       region plus every other usable region goes to the frame free-list. */
    const uint64_t HEAP_MAX = 16u * 1024 * 1024;
    uint64_t heap_end = start + (end - start > HEAP_MAX ? HEAP_MAX : (end - start));
    heap_end &= ~(PAGE_SIZE - 1);

    heap_base = start + hhdm;              /* HHDM-mapped virtual address */
    heap_size = heap_end - start;
    heap_used = 0;
    heap_phys_base = start;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t* e = entries[i];
        uint64_t base = e[0], len = e[1], type = e[2];
        if (type != 0) continue;
        if (base < 0x100000ULL) continue;
        if (base >= heap_phys_base && base + len <= heap_end) continue;
        uint64_t r0 = base, r1 = base + len;
        if (r0 < heap_phys_base) { frame_add_range(r0, heap_phys_base - r0); }
        if (r1 > heap_end) { frame_add_range(heap_end, r1 - heap_end); }
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
    uint64_t phys = (uint64_t)addr - hhdm;
    if (phys % PAGE_SIZE || phys >= FRAME_MAX) return 0;
    if (bmap_get(phys / PAGE_SIZE)) return 0;   /* already free */
    bmap_set(phys / PAGE_SIZE);
    free_frames++;
    return 1;
}

int64_t k_mem_pages_free(void) { return (int64_t)free_frames; }

/* Fallback arena used until mem_init() sets up the real heap (the runtime
   calls kf_alloc very early, before mem_init runs). */
static uint8_t  fallback_arena[8192];
static uint64_t fallback_used = 0;

/* Runtime allocator hook (strong symbol, replaces the old static-buffer one). */
void* kf_alloc(size_t n) {
    if (n == 0) n = 1;
    n = (n + 15u) & ~(size_t)15u;
    if (heap_size > 0) {                          /* real heap ready */
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

/*  KengaOS — Buddy page allocator.
    Классический алгоритм Knuth/Demand Paging.
*/
#include "buddy.h"
#include "../lib/libc.h"
#include "../drivers/uart.h"
static const char CRNL[3] = {13, 10, 0};
#include "../vmm/vmm.h"

/* ============================================================
   Внутренние структуры
   ============================================================ */

/* Запись в free-list для заданного order. */
struct block {
    struct block *next;
    struct block *prev;
    /* далее при необходимости — metadata */
};

#define FREE_LIST_HEAD(level) free_lists[(level)]

/* Bitmap для отслеживания, какие блоки заняты (для coalesce).
   bit=1 → занят (или половинка занятого). */
static u8 *alloc_bitmap = NULL;
static u64 bitmap_size = 0;

/* Free lists для каждого order. */
static struct block *free_lists[MAX_ORDER + 1] = {0};

/* База управляемой памяти (физический адрес) и размер. */
static u64 mem_base = 0;
static u64 mem_size = 0;
static u64 mem_num_pages = 0;     /* всего страниц в управлении */

/* Прямая карта: физический адрес → индекс страницы.
   Упрощение: считаем, что управляемая память начинается с mem_base
   и непрерывна. */

static struct buddy_stats stats = {0};

/* ============================================================
   Вспомогательные функции
   ============================================================ */

static inline u64 addr_to_index(u64 addr) {
    return (addr - mem_base) >> PAGE_SHIFT;
}

static inline u64 index_to_addr(u64 idx) {
    return mem_base + (idx << PAGE_SHIFT);
}

static inline u64 buddy_of(u64 idx, u8 order) {
    return idx ^ (1ULL << order);
}

static inline u8 min_order_for(u64 npages) {
    u8 order = 0;
    while ((1ULL << order) < npages) order++;
    return order;
}

/* Bitmap: bit 1 = занят/выделен, bit 0 = свободен.
   На каждый блок максимального order свой бит. */
static void bitmap_set(u64 idx, u8 order, bool val) {
    /* индекс в bitmap = idx / (2^order), бит в пределах блока */
    u64 block_idx = idx >> order;
    u64 byte = block_idx >> 3;
    u8  bit  = block_idx & 7;
    if (byte >= bitmap_size) return;
    if (val) alloc_bitmap[byte] |= (1 << bit);
    else     alloc_bitmap[byte] &= ~(1 << bit);
}

static bool bitmap_get(u64 idx, u8 order) {
    u64 block_idx = idx >> order;
    u64 byte = block_idx >> 3;
    u8  bit  = block_idx & 7;
    if (byte >= bitmap_size) return false;
    return (alloc_bitmap[byte] >> bit) & 1;
}

/* ============================================================
   Push / Pop из free-list
   ============================================================ */

static void free_list_push(u8 order, u64 addr) {
    /* Limine не identity-mappит всю RAM: пишем через HHDM. */
    struct block *b = (struct block *)phys_to_virt(addr);
    b->next = free_lists[order];
    b->prev = NULL;
    if (free_lists[order]) {
        free_lists[order]->prev = b;
    }
    free_lists[order] = b;
    stats.free_per_order[order]++;
}

static u64 free_list_pop(u8 order) {
    if (!free_lists[order]) return 0;
    struct block *b = free_lists[order];
    free_lists[order] = b->next;
    if (free_lists[order]) free_lists[order]->prev = NULL;
    stats.free_per_order[order]--;
    /* Возвращаем физический адрес (b — HHDM-указатель). */
    return (u64)b - vmm_get_hhdm();
}

/* ============================================================
   Инициализация
   ============================================================ */

void buddy_init(struct limine_memmap_response *memmap) {
    if (!memmap) return;

    /* Найти наибольший usable-сегмент — это будет наша основная зона. */
    u64 best_base = 0, best_len = 0;
    for (u64 i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        if (e->length > best_len) {
            best_base = e->base;
            best_len  = e->length;
        }
    }
    if (best_len == 0) return;

    /* Вычесть занятые диапазоны (ядро, initrd): Limine кладёт их в верх
       usable-сегмента — раздача этих страниц корёжит ядро (DMA писал в код).
       Обрезаем КОНЕЦ зоны до начала самого нижнего занятого блока. */
    {
        extern struct limine_module_request limine_module_request;
        extern char __kernel_start[], __kernel_end[];
        u64 cut = best_base + best_len;
        {
            /* физ. база ядра — трансляцией его же виртуального адреса */
            u64 kphys = virt_to_phys((u64)__kernel_start);
            u64 kend = kphys + (u64)(__kernel_end - __kernel_start);
            if (kphys >= best_base && kphys < cut) cut = kphys;
            if (kend > best_base && kend < best_base + best_len && kphys < best_base) {
                /* ядро в начале сегмента — сдвинуть базу за его конец */
                best_base = (kend + 0xFFF) & ~0xFFFULL;
                best_len -= best_base - (kend & ~0xFFFULL);
                best_len = (cut > best_base ? cut - best_base : 0);
            }
        }
        if (limine_module_request.response) {
            struct limine_module_response *mr = limine_module_request.response;
            for (u64 m = 0; m < mr->module_count; m++) {
                struct limine_file *f = mr->modules[m];
                if ((u64)f->address >= best_base && (u64)f->address < cut)
                    cut = (u64)f->address;
            }
        }
        {
            extern void uart_puts(unsigned short, const char *);
            uart_puts(0x3F8, "[buddy] zone=");
            char hb[20]; char hd[20];
            u64 vv = best_base; u32 n = 0;
            if (!vv) hb[n++] = 48;
            while (vv) { hb[n++] = 48 + vv % 10; vv /= 10; }
            for (u32 q = 0; q < n; q++) hd[q] = hb[n-1-q];
            hd[n] = 0;
            uart_puts(0x3F8, hd);
            uart_puts(0x3F8, " cut=");
            vv = cut; n = 0;
            if (!vv) hb[n++] = 48;
            while (vv) { hb[n++] = 48 + vv % 10; vv /= 10; }
            for (u32 q = 0; q < n; q++) hd[q] = hb[n-1-q];
            hd[n] = 0;
            uart_puts(0x3F8, hd);
            uart_puts(0x3F8, " kphys=");
            vv = virt_to_phys((u64)__kernel_start); n = 0;
            if (!vv) hb[n++] = 48;
            while (vv) { hb[n++] = 48 + vv % 10; vv /= 10; }
            for (u32 q = 0; q < n; q++) hd[q] = hb[n-1-q];
            hd[n] = 0;
            uart_puts(0x3F8, hd);
            uart_puts(0x3F8, " kern=");
            vv = (u64)(__kernel_end - __kernel_start); n = 0;
            if (!vv) hb[n++] = 48;
            while (vv) { hb[n++] = 48 + vv % 10; vv /= 10; }
            for (u32 q = 0; q < n; q++) hd[q] = hb[n-1-q];
            hd[n] = 0;
            uart_puts(0x3F8, hd);
            uart_puts(0x3F8, CRNL);
        }
        /* Ядро Limine кладёт рядом с модулями (обычно ниже): резервуем
           2 МБ под самым нижним модулем — гарантированно вне зоны. */
        if (cut < best_base + (2ULL << 20)) return;   /* зона слишком мала */
        cut -= 2ULL << 20;
        best_len = cut - best_base;
        if (best_len == 0) return;
    }

    /* Выровнять base вверх до PAGE_SIZE. */
    u64 aligned_base = (best_base + PAGE_SIZE - 1) & ~((u64)PAGE_SIZE - 1);
    u64 aligned_len  = best_len - (aligned_base - best_base);
    aligned_len &= ~((u64)PAGE_SIZE - 1);

    mem_base = aligned_base;
    mem_size = aligned_len;
    mem_num_pages = aligned_len >> PAGE_SHIFT;

    /* Выделить bitmap. Упрощение: выделяем bitmap в начале нашего же региона.
       Для max_order=10 нужно mem_num_pages/(2^10 * 8) байт. */
    u64 bitmap_bytes = (mem_num_pages >> 3) + 1;
    /* Округлить вверх до PAGE_SIZE */
    u64 bitmap_pages = (bitmap_bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;

    alloc_bitmap = (u8 *)phys_to_virt(mem_base);
    bitmap_size = bitmap_pages * PAGE_SIZE;
    kmemset(alloc_bitmap, 0, bitmap_size);

    /* Сдвинуть base — bitmap сам занимает место. */
    mem_base += bitmap_pages * PAGE_SIZE;
    mem_size -= bitmap_pages * PAGE_SIZE;
    mem_num_pages = mem_size >> PAGE_SHIFT;

    /* Разбить всю память на блоки максимального order и положить в free-list. */
    u64 idx = 0;
    while (idx < mem_num_pages) {
        /* Найти максимальный order, при котором блок начинается
           по выровненному адресу и помещается в оставшуюся память. */
        u8 order = MAX_ORDER;
        while (order > 0) {
            u64 block_pages = 1ULL << order;
            u64 block_mask = block_pages - 1;
            /* блок должен начинаться выровненно */
            if ((idx & block_mask) != 0) { order--; continue; }
            /* блок должен помещаться */
            if (idx + block_pages > mem_num_pages) { order--; continue; }
            break;
        }
        u64 addr = index_to_addr(idx);
        free_list_push(order, addr);
        bitmap_set(idx, order, false);
        idx += (1ULL << order);
    }

    stats.total_pages = mem_num_pages;
    stats.free_pages = mem_num_pages;
    stats.allocated_pages = 0;
}

/* ============================================================
   Выделение / освобождение
   ============================================================ */

static u64 try_alloc_order(u8 order) {
    if (order > MAX_ORDER) return 0;
    u64 addr = free_list_pop(order);
    if (addr == 0) {
        /* Попробовать больший order и split. */
        if (order == MAX_ORDER) return 0;
        u64 bigger = try_alloc_order(order + 1);
        if (bigger == 0) return 0;
        /* Положить вторую половину назад. */
        u64 buddy_addr = bigger + (1ULL << (order + PAGE_SHIFT));
        free_list_push(order, buddy_addr);
        bitmap_set(addr_to_index(bigger), order + 1, true);
        bitmap_set(addr_to_index(bigger), order, true);
        bitmap_set(addr_to_index(buddy_addr), order, false);
        return bigger;
    }
    bitmap_set(addr_to_index(addr), order, true);
    stats.allocated_pages += (1ULL << order);
    stats.free_pages -= (1ULL << order);
    return addr;
}

u64 buddy_alloc(u8 order) {
    if (order > MAX_ORDER) return 0;
    return try_alloc_order(order);
}

void buddy_free(u64 addr, u8 order) {
    if (order > MAX_ORDER) return;
    if (addr == 0) return;

    u64 idx = addr_to_index(addr);
    bitmap_set(idx, order, false);
    stats.allocated_pages -= (1ULL << order);
    stats.free_pages += (1ULL << order);

    /* Coalesce: пока есть свободный buddy того же order. */
    while (order < MAX_ORDER) {
        u64 buddy_idx = buddy_of(idx, order);
        if (buddy_idx >= mem_num_pages) break;
        if (bitmap_get(buddy_idx, order)) break;   /* buddy занят */

        /* Убрать buddy из free-list, объединить. */
        u64 buddy_addr = index_to_addr(buddy_idx);
        struct block *b = (struct block *)phys_to_virt(buddy_addr);
        if (b->prev) b->prev->next = b->next;
        else         free_lists[order] = b->next;
        if (b->next) b->next->prev = b->prev;
        stats.free_per_order[order]--;

        /* Перейти к меньшему idx (для сохранения инварианта). */
        if (buddy_idx < idx) idx = buddy_idx;
        order++;
        bitmap_set(idx, order, false);
        bitmap_set(buddy_of(idx, order - 1), order - 1, false);
    }
    free_list_push(order, index_to_addr(idx));
}

u64 buddy_alloc_pages(u64 npages) {
    if (npages == 0) return 0;
    u8 order = min_order_for(npages);
    if (order > MAX_ORDER) return 0;
    return buddy_alloc(order);
}

/* ============================================================
   Статистика
   ============================================================ */

void buddy_stats_get(struct buddy_stats *out) {
    if (!out) return;
    *out = stats;
    /* Пересчитать free_pages из free_lists на случай рассинхронизации. */
    u64 actual_free = 0;
    for (u8 o = 0; o <= MAX_ORDER; o++) {
        actual_free += stats.free_per_order[o] * (1ULL << o);
    }
    out->free_pages = actual_free;
    out->allocated_pages = stats.total_pages - actual_free;
}

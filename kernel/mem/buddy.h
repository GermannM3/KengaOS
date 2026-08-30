/*  KengaOS — Buddy page allocator.
    Управляет физической памятью, полученной от Limine memmap.

    Особенности:
      - Размер страницы = 4 КБ (PAGE_SIZE)
      - Min order = 0 (1 страница, 4 КБ)
      - Max order = MAX_ORDER (по умолчанию 10 = 4 МБ)
      - Free lists для каждого order
      - Split при выделении большего, чем есть
      - Coalesce при освобождении

    Не управляет:
      - Высоким half-адресом — возвращает ФИЗИЧЕСКИЕ адреса
        (вызывающий должен сам их маппировать, если надо).
      - Зонами DMA — упрощённая модель, вся usable-память как одна зона.
*/
#ifndef KENGA_BUDDY_H
#define KENGA_BUDDY_H

#include "../lib/types.h"
#include "../arch/x86_64/limine.h"

#define PAGE_SIZE       4096u
#define PAGE_SHIFT      12
#define MAX_ORDER       10   /* 2^10 * 4KB = 4 MB max block */

/* Инициализировать allocator по memmap'у от Limine. */
void buddy_init(struct limine_memmap_response *memmap);

/* Выделить 2^order страниц. Возвращает физический адрес или 0 при ошибке. */
u64 buddy_alloc(u8 order);

/* Освободить 2^order страниц по физическому адресу addr. */
void buddy_free(u64 addr, u8 order);

/* Статистика для диагностики. */
struct buddy_stats {
    u64 total_pages;       /* всего страниц в управлении */
    u64 free_pages;        /* свободно сейчас */
    u64 allocated_pages;   /* занято */
    u64 free_per_order[MAX_ORDER + 1];
};
void buddy_stats_get(struct buddy_stats *out);

/* Утилита: выделить ровно npages (округление вверх до степени 2). */
u64 buddy_alloc_pages(u64 npages);

#endif

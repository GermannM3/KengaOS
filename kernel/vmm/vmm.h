/*  KengaOS — Virtual Memory Manager.
    4-level paging для x86_64 (PML4, PDPT, PD, PT).

    Layout адресного пространства процесса:
      0x0000000000000000 - 0x0000007FFFFFFFFF   user (128 TB)
        0x0000000000000000 - 0x0000000000FFFFFF   code+data (16 MB)
        0x0000000001000000 - 0x0000000001FFFFFF   heap (16 MB)
        0x0000000002000000 - 0x0000000002FFFFFF   stack (16 MB, растёт вниз)
      0xFFFFFFFF80000000 - ...                    kernel (higher half)
        Доступен всем процессам через copy-on-share
*/
#ifndef KENGA_VMM_H
#define KENGA_VMM_H

#include "../lib/types.h"

#define PAGE_SIZE   4096u
#define PAGE_SHIFT  12
#define PAGE_PRESENT  (1u << 0)
#define PAGE_WRITABLE (1u << 1)
#define PAGE_USER     (1u << 2)
#define PAGE_NX       (1ULL << 63)   /* No-eXecute: ставить на неисполняемых страницах */

/* Адреса в user-space */
#define USER_CODE_BASE  0x0000000000100000ULL   /* 1 MB (typical ELF load addr) */
#define USER_CODE_SIZE  0x0000000000100000ULL   /* 1 MB max code */
#define USER_HEAP_BASE  0x0000000000400000ULL   /* 4 MB */
#define USER_HEAP_SIZE  0x0000000000400000ULL   /* 4 MB heap */
#define USER_STACK_TOP  0x0000000000800000ULL   /* 8 MB (excluded) */
#define USER_STACK_SIZE 0x0000000000100000ULL   /* 1 MB stack */

/* Create новый PML4 для процесса. Возвращает физический адрес PML4. */
u64 vmm_create_address_space(void);

/* Уничтожить address space. */
void vmm_destroy_address_space(u64 pml4_phys);

/* Сменить текущий address space (CR3). */
void vmm_switch_to(u64 pml4_phys);

/* Смапить страницу: virt → phys с flags. */
bool vmm_map_page(u64 pml4_phys, u64 virt, u64 phys, u64 flags);

/* Снять страницу. */
void vmm_unmap_page(u64 pml4_phys, u64 virt);

/* Смапить несколько страниц. */
bool vmm_map_range(u64 pml4_phys, u64 virt, u64 phys, u64 count, u64 flags);

/* Активировать paging (вызывается один раз при boot). */
void vmm_init(void);

/* Текущий PML4 (kernel). */
u64 vmm_kernel_pml4(void);

/* HHDM offset (из Limine). */
void vmm_set_hhdm(u64 offset);
u64 vmm_get_hhdm(void);

/* Преобразовать физ → вирт через HHDM. */
void *phys_to_virt(u64 phys);

/* Физический адрес виртуального (по текущим таблицам). */
u64 virt_to_phys(u64 virt);

#endif

/*  KengaOS — VMM (Virtual Memory Manager).
    4-level paging: PML4 → PDPT → PD → PT → page.

    Использует HHDM (Higher Half Direct Map) от Limine для доступа
    к физическим страницам — все физ. адреса доступны через
    (phys + hhdm_offset) в kernel-space.
*/
#include "vmm.h"
#include "../lib/libc.h"
#include "../mem/buddy.h"
#include "../arch/x86_64/limine.h"
#include "../arch/x86_64/io.h"

/* ============================================================
    Page table entry structure (x86_64)
    Bit 0: Present
    Bit 1: Writable
    Bit 2: User
    Bit 7: PS (page size — 1 для 2MB страниц)
    Bit 63: NX (No-Execute, если установлен)
   ============================================================ */

#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)

static u64 hhdm_offset = 0;
static u64 kernel_pml4 = 0;

void vmm_set_hhdm(u64 offset) { hhdm_offset = offset; }
u64 vmm_get_hhdm(void) { return hhdm_offset; }

void *phys_to_virt(u64 phys) {
    return (void*)(phys + hhdm_offset);
}

/* Получить указатель на PML4 (текущий, через CR3). */
static u64 read_cr3(void) {
    u64 v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v & ~0xFFFULL;   /* маска PCID и reserved bits */
}

static void write_cr3(u64 v) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(v & ~0xFFFULL));
}

/* Включить paging + NXE (если поддерживается). */
static void enable_paging(void) {
    u64 cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    /* PAE уже включён Limine. */
}

/* Включить NXE bit в EFER (для поддержки NX bit в PTE). */
static void enable_nxe(void) {
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));   /* EFER */
    u64 efer = ((u64)hi << 32) | lo;
    efer |= (1ULL << 11);   /* NXE bit */
    lo = efer & 0xFFFFFFFF;
    hi = efer >> 32;
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000080));
}

/* ============================================================
   Map / unmap
   ============================================================ */

/* Получить или создать PT entry по виртуальному адресу.
   Возвращает указатель на PTE (в HHDM-пространстве) или NULL. */
static volatile u64 *get_or_create_pte(u64 pml4_phys, u64 virt, bool create) {
    u64 *pml4 = (u64*)phys_to_virt(pml4_phys);

    u64 pml4e = pml4[PML4_INDEX(virt)];
    u64 *pdpt;
    if (!(pml4e & PAGE_PRESENT)) {
        if (!create) return NULL;
        /* Аллоцировать новую PDPT (1 page) */
        u64 pdpt_phys = buddy_alloc(0);   /* 4 KB = order 0 */
        if (pdpt_phys == 0) return NULL;
        kmemset(phys_to_virt(pdpt_phys), 0, PAGE_SIZE);
        pml4[PML4_INDEX(virt)] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        pdpt = (u64*)phys_to_virt(pdpt_phys);
    } else {
        pdpt = (u64*)phys_to_virt(pml4e & ~0xFFFULL);
    }

    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    u64 *pd;
    if (!(pdpte & PAGE_PRESENT)) {
        if (!create) return NULL;
        u64 pd_phys = buddy_alloc(0);
        if (pd_phys == 0) return NULL;
        kmemset(phys_to_virt(pd_phys), 0, PAGE_SIZE);
        pdpt[PDPT_INDEX(virt)] = pd_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        pd = (u64*)phys_to_virt(pd_phys);
    } else {
        pd = (u64*)phys_to_virt(pdpte & ~0xFFFULL);
    }

    u64 pde = pd[PD_INDEX(virt)];
    u64 *pt;
    if (!(pde & PAGE_PRESENT)) {
        if (!create) return NULL;
        u64 pt_phys = buddy_alloc(0);
        if (pt_phys == 0) return NULL;
        kmemset(phys_to_virt(pt_phys), 0, PAGE_SIZE);
        pd[PD_INDEX(virt)] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        pt = (u64*)phys_to_virt(pt_phys);
    } else {
        pt = (u64*)phys_to_virt(pde & ~0xFFFULL);
    }

    return &pt[PT_INDEX(virt)];
}

bool vmm_map_page(u64 pml4_phys, u64 virt, u64 phys, u64 flags) {
    volatile u64 *pte = get_or_create_pte(pml4_phys, virt, true);
    if (!pte) return false;
    /* Если уже смаплена — ошибка или обновить? Пока обновляем.
       flags: биты 0..11 — свойства, бит 63 — NX. */
    *pte = (phys & ~0xFFFULL) | PAGE_PRESENT | (flags & (0xFFFULL | PAGE_NX));
    /* Инвалидация TLB для этой страницы */
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

void vmm_unmap_page(u64 pml4_phys, u64 virt) {
    volatile u64 *pte = get_or_create_pte(pml4_phys, virt, false);
    if (!pte) return;
    *pte = 0;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

bool vmm_map_range(u64 pml4_phys, u64 virt, u64 phys, u64 count, u64 flags) {
    for (u64 i = 0; i < count; i++) {
        if (!vmm_map_page(pml4_phys, virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags)) {
            return false;
        }
    }
    return true;
}

u64 vmm_create_address_space(void) {
    /* Аллоцировать PML4 (1 page) */
    u64 pml4_phys = buddy_alloc(0);
    if (pml4_phys == 0) return 0;
    kmemset(phys_to_virt(pml4_phys), 0, PAGE_SIZE);

    /* Скопировать kernel mappings (upper half, PML4[256..511]) */
    u64 *kpml4 = (u64*)phys_to_virt(kernel_pml4);
    u64 *npml4 = (u64*)phys_to_virt(pml4_phys);
    for (int i = 256; i < 512; i++) {
        npml4[i] = kpml4[i];
    }

    return pml4_phys;
}

void vmm_destroy_address_space(u64 pml4_phys) {
    /* Освободить все user-страницы таблиц (PML4[0..255]).
       Не трогаем kernel-часть. */
    u64 *pml4 = (u64*)phys_to_virt(pml4_phys);
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & PAGE_PRESENT)) continue;
        u64 pdpt_phys = pml4[i] & ~0xFFFULL;
        u64 *pdpt = (u64*)phys_to_virt(pdpt_phys);
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & PAGE_PRESENT)) continue;
            u64 pd_phys = pdpt[j] & ~0xFFFULL;
            u64 *pd = (u64*)phys_to_virt(pd_phys);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PAGE_PRESENT)) continue;
                /* Если не 2MB huge page — освободить PT */
                if (!(pd[k] & (1 << 7))) {
                    u64 pt_phys = pd[k] & ~0xFFFULL;
                    buddy_free(pt_phys, 0);
                }
            }
            buddy_free(pd_phys, 0);
        }
        buddy_free(pdpt_phys, 0);
    }
    buddy_free(pml4_phys, 0);
}

void vmm_switch_to(u64 pml4_phys) {
    write_cr3(pml4_phys);
}

u64 vmm_kernel_pml4(void) {
    return kernel_pml4;
}

void vmm_init(void) {
    /* Получить HHDM от Limine */
    if (limine_hhdm_request.response) {
        hhdm_offset = (u64)limine_hhdm_request.response->offset;
    }

    /* Текущий CR3 — это kernel PML4 (его создал Limine). */
    kernel_pml4 = read_cr3();

    enable_nxe();
}

u64 virt_to_phys(u64 virt) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFULL;
    volatile u64 *pml4 = (volatile u64 *)phys_to_virt(cr3);
    u64 e = pml4[(virt >> 39) & 511];
    if (!(e & 1)) return 0;
    volatile u64 *pdpt = (volatile u64 *)phys_to_virt(e & ~0xFFFULL);
    e = pdpt[(virt >> 30) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) return (e & ~((1ULL << 30) - 1)) | (virt & ((1ULL << 30) - 1));
    volatile u64 *pd = (volatile u64 *)phys_to_virt(e & ~0xFFFULL);
    e = pd[(virt >> 21) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) return (e & ~((1ULL << 21) - 1)) | (virt & ((1ULL << 21) - 1));
    volatile u64 *pt = (volatile u64 *)phys_to_virt(e & ~0xFFFULL);
    e = pt[(virt >> 12) & 511];
    if (!(e & 1)) return 0;
    return (e & ~0xFFFULL) | (virt & 0xFFF);
}

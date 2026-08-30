/*  KengaOS — ELF64 loader.
    Парсит ELF-заголовок, копирует PT_LOAD сегменты в user-space,
    возвращает entry point.
*/
#include "elf.h"
#include "../lib/libc.h"
#include "../vmm/vmm.h"
#include "../mem/buddy.h"

/* ELF64 header (упрощённый, только нужные поля) */
struct elf64_ehdr {
    u8  ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
} __attribute__((packed));

#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1

#define PF_X 1
#define PF_W 2
#define PF_R 4

bool elf_load(struct vfs_file *file, u64 pml4_phys, struct elf_info *out) {
    if (file->size < sizeof(struct elf64_ehdr)) return false;

    u8 *data = file->data;
    struct elf64_ehdr *eh = (struct elf64_ehdr*)data;

    /* Проверка magic */
    if (eh->ident[0] != 0x7F) return false;
    if (eh->ident[1] != 'E') return false;
    if (eh->ident[2] != 'L') return false;
    if (eh->ident[3] != 'F') return false;
    if (eh->ident[4] != 2) return false;   /* ELFCLASS64 */
    if (eh->ident[5] != 1) return false;   /* ELFDATA2LSB */
    if (eh->type != ET_EXEC) return false;
    if (eh->machine != EM_X86_64) return false;

    /* Проверить, что phoff и phnum валидны */
    if (eh->phoff == 0 || eh->phnum == 0) return false;
    if (eh->phoff + eh->phnum * sizeof(struct elf64_phdr) > file->size) return false;

    struct elf64_phdr *phdrs = (struct elf64_phdr*)(data + eh->phoff);

    /* Загрузить каждый PT_LOAD сегмент */
    for (u16 i = 0; i < eh->phnum; i++) {
        struct elf64_phdr *ph = &phdrs[i];
        if (ph->type != PT_LOAD) continue;

        /* Вычислить флаги страницы */
        u64 page_flags = PAGE_USER;
        if (ph->flags & PF_W) page_flags |= PAGE_WRITABLE;
        /* NX: если сегмент НЕ исполняемый — запретить исполнение */
        if (!(ph->flags & PF_X)) page_flags |= PAGE_NX;

        /* Округлить адреса до страниц */
        u64 vaddr = ph->vaddr & ~0xFFFULL;
        u64 vaddr_end = (ph->vaddr + ph->memsz + 0xFFF) & ~0xFFFULL;
        u64 num_pages = (vaddr_end - vaddr) / PAGE_SIZE;

        /* Скопировать данные сегмента во временную kernel-память */
        /* Сначала смапить страницы в user-space, потом скопировать данные */

        for (u64 p = 0; p < num_pages; p++) {
            u64 page_virt = vaddr + p * PAGE_SIZE;
            u64 page_phys = buddy_alloc(0);
            if (page_phys == 0) return false;

            /* Обнулить страницу (в kernel HHDM) */
            kmemset(phys_to_virt(page_phys), 0, PAGE_SIZE);

            /* Скопировать данные сегмента в эту страницу */
            u64 file_offset = ph->offset + p * PAGE_SIZE;
            if (file_offset < file->size) {
                u64 copy_size = PAGE_SIZE;
                if (file_offset + copy_size > file->size) {
                    copy_size = file->size - file_offset;
                }
                /* Учитывать смещение внутри страницы (если vaddr не выровнен) */
                u64 in_page_offset = (p == 0) ? (ph->vaddr & 0xFFF) : 0;
                if (in_page_offset + copy_size > PAGE_SIZE) {
                    copy_size = PAGE_SIZE - in_page_offset;
                }
                kmemcpy(
                    (u8*)phys_to_virt(page_phys) + in_page_offset,
                    data + file_offset,
                    copy_size
                );
            }

            /* Смапить страницу в user-space */
            if (!vmm_map_page(pml4_phys, page_virt, page_phys, page_flags)) {
                return false;
            }
        }
    }

    /* Создать user-стек */
    u64 stack_top = USER_STACK_TOP;
    u64 stack_base = stack_top - USER_STACK_SIZE;
    u64 stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    for (u64 p = 0; p < stack_pages; p++) {
        u64 page_virt = stack_base + p * PAGE_SIZE;
        u64 page_phys = buddy_alloc(0);
        if (page_phys == 0) return false;
        kmemset(phys_to_virt(page_phys), 0, PAGE_SIZE);
        if (!vmm_map_page(pml4_phys, page_virt, page_phys,
                          PAGE_USER | PAGE_WRITABLE)) {
            return false;
        }
    }

    if (out) {
        out->entry = eh->entry;
        out->stack_top = stack_top;
    }

    return true;
}

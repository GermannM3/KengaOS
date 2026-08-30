/*  KengaOS — ELF loader.
    Загружает static 64-bit ELF из initrd в user address space.

    Поддерживает:
      - ELF64 LSB executable
      - ET_EXEC (статический)
      - Загрузка сегментов PT_LOAD в user-space
*/
#ifndef KENGA_ELF_H
#define KENGA_ELF_H

#include "../lib/types.h"
#include "../fs/vfs.h"

/* Информация о загруженной программе. */
struct elf_info {
    u64 entry;            /* точка входа */
    u64 stack_top;        /* верх стека */
};

/* Загрузить ELF файл в адресное пространство процесса с PML4 = pml4_phys.
   Выделяет страницы через buddy.
   Возвращает true при успехе. */
bool elf_load(struct vfs_file *file, u64 pml4_phys, struct elf_info *out);

#endif

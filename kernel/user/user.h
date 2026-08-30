/*  KengaOS — User process launcher.
    Запускает ELF из initrd как ring 3 процесс.

    Последовательность:
      1. Найти файл в VFS
      2. Создать новый thread (struct thread, но без buddy-стека —
         свой стек будет в user-space)
      3. Создать address space (PML4)
      4. Загрузить ELF → мапит сегменты + стек в user-space
      5. Переключиться на user-mode через iretq (с RPL=3)
*/
#ifndef KENGA_USER_H
#define KENGA_USER_H

#include "../lib/types.h"

/* Запустить ELF файл из initrd как user-процесс.
   Возвращает thread_id или 0 при ошибке. */
u64 user_exec(const char *path);

#endif

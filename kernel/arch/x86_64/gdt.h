/*  KengaOS — GDT (Global Descriptor Table).
    Long mode: сегменты flat, но TSS нужен для ring switches.
    Ring 0 (kernel) + Ring 3 (user).
*/
#ifndef KENGA_GDT_H
#define KENGA_GDT_H

#include "../lib/types.h"

void gdt_init(void);

/* Установить RSP0 в TSS (используется при syscall/interrupt из ring 3 в ring 0). */
void gdt_set_rsp0(u64 rsp0);

#endif

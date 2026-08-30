/*  KengaOS — PIO IDE disk (программная передача через порты 0x1F0).
    Альтернатива AHCI: поллинг без DMA, работает в любом TCG/эмуляторе
    и на реальном железе (BIOS-путь). Primary master.
*/
#ifndef KENGA_PIOIDE_H
#define KENGA_PIOIDE_H

#include "../lib/types.h"

int  pioide_init(void);
u64  pioide_sectors(void);
int  pioide_read(u64 lba, u16 count, void *buf);   /* count <= 8 */
int  pioide_write(u64 lba, u16 count, const void *buf);

#endif

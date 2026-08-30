/*  KengaOS — AHCI (SATA) драйвер.
    Находит контроллер по PCI class 01:06, поднимает порт 0,
    IDENTIFY + секторный DMA read/write. Поллинг, без IRQ.
*/
#ifndef KENGA_AHCI_H
#define KENGA_AHCI_H

#include "../lib/types.h"

/* true если найден контроллер и диск отвечает */
int ahci_init(void);

/* Сектор = 512 байт. count <= 8 (одна страница DMA).
   Возвращают 0 при успехе. lba — 48-битный. */
int ahci_read(u64 lba, u16 count, void *buf);
int ahci_write(u64 lba, u16 count, const void *buf);

/* Число секторов диска (после IDENTIFY), 0 если нет диска. */
u64 ahci_sectors(void);

#endif

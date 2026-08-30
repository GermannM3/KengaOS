/*  KengaOS — PCI-конфигурация (доступ через порты 0xCF8/0xCFC).
    Фундамент для AHCI/NVMe/e1000/xHCI и будущих WiFi-карт на PCIe.
*/
#ifndef KENGA_PCI_H
#define KENGA_PCI_H

#include "../lib/types.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

typedef struct {
    u8  bus, dev, fn;
    u16 vendor, device;
    u8  class_code, subclass, prog_if;
    u8  irq;
    u32 bars[6];      /* декодированные BAR (адрес), 0 если нет */
    u8  bar_type[6];  /* 0 = MMIO32, 1 = MMIO64 (старшие в следующем), 2 = IO */
} pci_dev_t;

/* Прочитать 32-битное слово конфигурационного пространства. */
u32 pci_read32(u8 bus, u8 dev, u8 fn, u8 off);

/* Энумерация шины 0 (QEMU: одна шина). Возвращает количество найденных функций. */
u32 pci_scan(pci_dev_t *out, u32 max);

/* Найти устройство по класс/subclass. NULL если не найдено. */
const pci_dev_t *pci_find_class(u8 class_code, u8 subclass);

void pci_init(void);

#endif

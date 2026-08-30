/*  KengaOS — PCI-энумерация (шина 0,legacy-доступ CF8/CFC).
    QEMU: ich9 — AHCI на bus 0, все устройdev на одной шине.
*/
#include "pci.h"
#include "uart.h"
#include "../arch/x86_64/io.h"

u32 pci_read32(u8 bus, u8 dev, u8 fn, u8 off) {
    u32 addr = (1u << 31) | ((u32)bus << 16) | ((u32)dev << 11)
             | ((u32)fn << 8) | (off & 0xFC);
    outl(PCI_ADDR, addr);
    return inl(PCI_DATA);
}

static void pci_write32(u8 bus, u8 dev, u8 fn, u8 off, u32 val) {
    u32 addr = (1u << 31) | ((u32)bus << 16) | ((u32)dev << 11)
             | ((u32)fn << 8) | (off & 0xFC);
    outl(PCI_ADDR, addr);
    outl(PCI_DATA, val);
}

/* Включить устройство: MMIO + bus master (нужно для DMA AHCI). */
static void pci_enable(const pci_dev_t *d) {
    u32 cmd = pci_read32(d->bus, d->dev, d->fn, 4);
    cmd |= 0x7;   /* IO + MMIO + BusMaster */
    pci_write32(d->bus, d->dev, d->fn, 4, cmd);
}

u32 pci_scan(pci_dev_t *out, u32 max) {
    u32 n = 0;
    for (u8 dev = 0; dev < 32 && n < max; dev++) {
        u8 fns = 1;
        for (u8 fn = 0; fn < fns && n < max; fn++) {
            u32 vd = pci_read32(0, dev, fn, 0);
            if ((vd & 0xFFFF) == 0xFFFF) continue;
            pci_dev_t *d = &out[n++];
            d->bus = 0; d->dev = dev; d->fn = fn;
            d->vendor = vd & 0xFFFF;
            d->device = vd >> 16;
            u32 cl = pci_read32(0, dev, fn, 8);
            d->class_code = cl >> 24;
            d->subclass = (cl >> 16) & 0xFF;
            d->prog_if = (cl >> 8) & 0xFF;
            d->irq = pci_read32(0, dev, fn, 0x3C) & 0xFF;
            /* заголовок типа: multifunction? */
            u8 ht = (pci_read32(0, dev, fn, 0xC) >> 16) & 0x7F;
            if (fn == 0 && (ht & 0x80)) fns = 8;
            /* BAR'ы */
            for (int b = 0; b < 6; b++) {
                u32 raw = pci_read32(0, dev, fn, 0x10 + b * 4);
                if (!raw) { d->bars[b] = 0; d->bar_type[b] = 0; continue; }
                if (raw & 1) {                       /* I/O space */
                    d->bars[b] = raw & ~0x3;
                    d->bar_type[b] = 2;
                } else if ((raw & 0x6) == 0x4) {     /* MMIO64 */
                    u32 hi = pci_read32(0, dev, fn, 0x14 + b * 4);
                    d->bars[b] = (raw & ~0xF) | (hi << 32); /* low32|hi */
                    d->bar_type[b] = 1;
                    b++;  /* старший dword пропускаем */
                } else {
                    d->bars[b] = raw & ~0xF;
                    d->bar_type[b] = 0;
                }
            }
            pci_enable(d);
        }
    }
    return n;
}

#define PCI_NDEV 24
static pci_dev_t pci_devices[PCI_NDEV];
static u32 pci_count = 0;

const pci_dev_t *pci_find_class(u8 class_code, u8 subclass) {
    for (u32 i = 0; i < pci_count; i++)
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass)
            return &pci_devices[i];
    return 0;
}

void pci_init(void) {
    pci_count = pci_scan(pci_devices, PCI_NDEV);
    uart_puts(UART_COM1, "\r\n[pci] устройств: ");
    char b[8]; u32 v = pci_count, t = 0;
    char tmp[8];
    if (!v) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + v % 10; v /= 10; }
    for (u32 i = 0; i < t; i++) b[i] = tmp[t - 1 - i];
    b[t] = 0;
    uart_puts(UART_COM1, b);
    uart_puts(UART_COM1, "\r\n");
    for (u32 i = 0; i < pci_count; i++) {
        pci_dev_t *d = &pci_devices[i];
        if (!d->class_code) continue;   /* host bridge не печатаем */
        uart_puts(UART_COM1, "[pci] 00:");
        uart_putc(UART_COM1, '0' + d->dev / 10);
        uart_putc(UART_COM1, '0' + d->dev % 10);
        uart_putc(UART_COM1, '.');
        uart_putc(UART_COM1, '0' + d->fn);
        uart_puts(UART_COM1, " class=");
        uart_putc(UART_COM1, "0123456789ABCDEF"[d->class_code >> 4]);
        uart_putc(UART_COM1, "0123456789ABCDEF"[d->class_code & 0xF]);
        uart_putc(UART_COM1, "0123456789ABCDEF"[d->subclass >> 4]);
        uart_putc(UART_COM1, "0123456789ABCDEF"[d->subclass & 0xF]);
        uart_puts(UART_COM1, " vid=");
        uart_putc(UART_COM1, "0123456789ABCDEF"[(d->vendor >> 12) & 0xF]);
        uart_putc(UART_COM1, "0123456789ABCDEF"[(d->vendor >> 8) & 0xF]);
        uart_putc(UART_COM1, "0123456789ABCDEF"[(d->vendor >> 4) & 0xF]);
        uart_putc(UART_COM1, "0123456789ABCDEF"[d->vendor & 0xF]);
        uart_putc(UART_COM1, ':');
        uart_putc(UART_COM1, "0123456789ABCDEF"[(d->device >> 12) & 0xF]);
        uart_putc(UART_COM1, "0123456789ABCDEF"[(d->device >> 8) & 0xF]);
        uart_putc(UART_COM1, "0123456789ABCDEF"[(d->device >> 4) & 0xF]);
        uart_putc(UART_COM1, "0123456789ABCDEF"[d->device & 0xF]);
        uart_puts(UART_COM1, "\r\n");
    }
}

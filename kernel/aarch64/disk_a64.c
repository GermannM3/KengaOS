/* disk_a64.c — заглушки k_disk_* для aarch64.
 *
 * kf_disk.c — x86 ATA PIO (порты 0x1F0/0x170); на QEMU virt и ARM-платах
 * этого контроллера нет. Пока: init=0 (kmain честно печатает disk_init=FAIL).
 * Апгрейд: virtio-blk (PCI) — диск этапа 3, поверх xHCI-стиля MMIO.
 */
#include "kf_rt.h"

int64_t k_disk_init(void)    { return 0; }
int64_t k_disk_sectors(void) { return 0; }
int64_t k_disk_read(uint64_t lba, uint16_t count, void* buf) { (void)lba; (void)count; (void)buf; return 0; }
int64_t k_disk_write(uint64_t lba, uint16_t count, const void* buf) { (void)lba; (void)count; (void)buf; return 0; }

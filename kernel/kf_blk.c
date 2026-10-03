/* kf_blk.c — блок-устройство: FFI-мост между KengaFS (kernel/fs.kenga)
 * и дисковым слоем. k_disk_read/write — арх-реализация (x86: ATA PIO в
 * kf_disk.c; aarch64: virtio-blk в aarch64/disk_a64.c). Обёртки *64
 * принимают i64-адреса, чтобы их можно было вызвать прямо из Kenga.
 * Вся логика ФС — на Kenga; здесь только 2 перехода через границу FFI. */
#include "kf_rt.h"

int64_t k_disk_read(uint64_t lba, uint16_t count, void* buf);
int64_t k_disk_write(uint64_t lba, uint16_t count, const void* buf);

int64_t k_disk_read64(int64_t lba, int64_t count, int64_t buf) {
    return k_disk_read((uint64_t)lba, (uint16_t)count, (void*)(uintptr_t)buf);
}

int64_t k_disk_write64(int64_t lba, int64_t count, int64_t buf) {
    return k_disk_write((uint64_t)lba, (uint16_t)count, (const void*)(uintptr_t)buf);
}

/* k_vfs_cat с i64-адресом приёмника: Kenga копирует файл initrd в кадр,
   чтобы затем записать его в KengaFS (например, user-ELF в /apps). */
int64_t k_vfs_cat64(const char* name, int64_t out, int64_t max) {
    return k_vfs_cat(name, (char*)(uintptr_t)out, (int)max);
}

/* disk_a64.c — k_disk_* для aarch64 поверх virtio-blk (virtio_a64.c).
 * kf_disk.c — x86 ATA PIO; на QEMU virt диск — virtio-mmio (0x0a000000+).
 * rw-тест: пишем только в диск с маркером KENGARWTEST1 на LBA0
 * (scratch-диск из scripts/run-a64.sh); ESP/пользовательские — skip.
 */
#include "kf_rt.h"

int64_t k_vblk_init(void);
int     k_vblk_count(void);
uint64_t k_vblk_sectors(int idx);
int     k_vblk_read(int idx, uint64_t lba, uint16_t count, void* buf);
int     k_vblk_write(int idx, uint64_t lba, uint16_t count, const void* buf);
int     k_vblk_find_marker(void);

static int64_t vblk_dbg = -999;
int64_t k_disk_dbg(void) { return vblk_dbg; }
int64_t k_disk_init(void) {
    int64_t r = k_vblk_init();
    uint8_t b[512];
    int rr = k_vblk_read(0, 0, 1, b);
    if (rr) vblk_dbg = -100 - rr;
    else vblk_dbg = (int64_t)b[0] | ((int64_t)b[1] << 8) | ((int64_t)b[2] << 16) | ((int64_t)b[3] << 24);
    return r;
}
int64_t k_disk_sectors(void) { return (int64_t)k_vblk_sectors(0); }
int64_t k_disk_read(uint64_t lba, uint16_t count, void* buf) {
    return k_vblk_read(0, lba, count, buf);
}
int64_t k_disk_write(uint64_t lba, uint16_t count, const void* buf) {
    return k_vblk_write(0, lba, count, buf);
}

/* RW-тест: маркер на LBA0 scratch-диска, хвост — write/verify/restore */
int64_t k_disk_rw_test(void) {
    static uint8_t orig[512], pat[512], back[512];
    int idx = k_vblk_find_marker();
    if (idx < 0) return 2;
    uint64_t n = k_vblk_sectors(idx);
    if (n < 32) return 0;
    uint64_t lba = n - 16;
    if (k_vblk_read(idx, lba, 1, orig)) return -1;
    for (int i = 0; i < 512; i++)
        pat[i] = (uint8_t)(0x4B ^ (i * 7) ^ (lba));
    if (k_vblk_write(idx, lba, 1, pat)) return -2;
    if (k_vblk_read(idx, lba, 1, back)) return -3;
    for (int i = 0; i < 512; i++) if (pat[i] != back[i]) return -4;
    if (k_vblk_write(idx, lba, 1, orig)) return -5;
    if (k_vblk_read(idx, lba, 1, back)) return -6;
    for (int i = 0; i < 512; i++) if (orig[i] != back[i]) return -7;
    return 1;
}

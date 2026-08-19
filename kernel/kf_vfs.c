/* kf_vfs.c — minimal in-kernel virtual filesystem (M2.8).
 *
 * A read-only table of virtual files (name + content), enough to give the
 * shell `ls` / `cat`. A real initrd/module-backed VFS comes later.
 */
#include "kf_rt.h"

typedef struct { const char* name; const char* content; uint64_t size; } vfs_file;

#define VFS_MAX 32
static vfs_file vfs_files[VFS_MAX];
static int      vfs_count = 0;

/* initrd format (little-endian):
     u32 file_count, then per file: u32 name_len, name, u32 size, data. */
static uint8_t* rd_base = 0;

int64_t k_vfs_init_rd(int64_t addr, int64_t size) {
    rd_base = (uint8_t*)(uintptr_t)addr;
    if (!rd_base || size < 4) return 0;
    uint32_t count = *(uint32_t*)rd_base;
    uint8_t* p = rd_base + 4;
    vfs_count = 0;
    for (uint32_t i = 0; i < count && vfs_count < VFS_MAX; i++) {
        uint32_t nlen = *(uint32_t*)p; p += 4;
        const char* name = (const char*)p; p += nlen;
        uint32_t dlen = *(uint32_t*)p; p += 4;
        const char* data = (const char*)p; p += dlen;
        vfs_file* f = &vfs_files[vfs_count];
        f->name = name;
        f->content = data;
        f->size = dlen;
        vfs_count++;
    }
    return vfs_count;
}

int64_t k_vfs_count(void) { return vfs_count; }

const char* k_vfs_name(int64_t idx) {
    if (idx < 0 || idx >= vfs_count) return "";
    return vfs_files[idx].name;
}

/* Find a file by name; returns 1 and fills content if found. */
int64_t k_vfs_cat(const char* name, char* out, int max) {
    for (int i = 0; i < vfs_count; i++) {
        const char* a = name; const char* b = vfs_files[i].name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) {
            int k = 0;
            for (; k < (int)vfs_files[i].size && k < max - 1; k++) out[k] = vfs_files[i].content[k];
            out[k] = 0;
            return 1;
        }
    }
    return 0;
}

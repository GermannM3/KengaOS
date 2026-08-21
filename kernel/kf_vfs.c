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

static uint32_t rd_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int64_t k_vfs_init_rd(int64_t addr, int64_t size) {
    rd_base = (uint8_t*)(uintptr_t)addr;
    if (!rd_base || size < 4) return 0;
    uint8_t* end = rd_base + (uint64_t)size;
    if (end < rd_base) return 0;
    uint32_t count = rd_u32(rd_base);
    uint8_t* p = rd_base + 4;
    vfs_count = 0;
    for (uint32_t i = 0; i < count && vfs_count < VFS_MAX; i++) {
        if ((uint64_t)(end - p) < 4) { vfs_count = 0; return 0; }
        uint32_t nlen = rd_u32(p); p += 4;
        if (nlen == 0 || (uint64_t)(end - p) < (uint64_t)nlen + 4u) { vfs_count = 0; return 0; }
        const char* name = (const char*)p; p += nlen;
        uint32_t dlen = rd_u32(p); p += 4;
        if ((uint64_t)(end - p) < dlen) { vfs_count = 0; return 0; }
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

/* Address of the "version" file content (for the boot screen). */
int64_t k_vfs_version_addr(void) {
    for (int i = 0; i < vfs_count; i++) {
        const char* n = vfs_files[i].name;
        if (n && n[0]=='v' && n[1]=='e' && n[2]=='r' && n[3]=='s' && n[4]=='i' && n[5]=='o' && n[6]=='n' && n[7]==0)
            return (int64_t)(uintptr_t)vfs_files[i].content;
    }
    return 0;
}

const char* k_vfs_name(int64_t idx) {
    if (idx < 0 || idx >= vfs_count) return "";
    return vfs_files[idx].name;
}

/* Find a file by name; returns 1 and fills content if found. */
int64_t k_vfs_cat(const char* name, char* out, int max) {
    if (!name || !out || max <= 0) return 0;
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

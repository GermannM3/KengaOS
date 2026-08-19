/* kf_vfs.c — minimal in-kernel virtual filesystem (M2.8).
 *
 * A read-only table of virtual files (name + content), enough to give the
 * shell `ls` / `cat`. A real initrd/module-backed VFS comes later.
 */
#include "kf_rt.h"

typedef struct { const char* name; const char* content; } vfs_file;

static const vfs_file vfs_files[] = {
    { "version", "KengaOS 0.1 (x86_64)\n" },
    { "cpu",     "64-bit x86_64, long mode\n" },
    { "kernel",  "Kenga kernel over Limine, framebuffer console\n" },
    { "help",    "commands: help info clear echo mem ps log ask tasks ls cat\n" },
};

#define VFS_COUNT ((int)(sizeof(vfs_files) / sizeof(vfs_files[0])))

int64_t k_vfs_count(void) { return VFS_COUNT; }

const char* k_vfs_name(int64_t idx) {
    if (idx < 0 || idx >= VFS_COUNT) return "";
    return vfs_files[idx].name;
}

/* Find a file by name; returns 1 and fills content if found. */
int64_t k_vfs_cat(const char* name, char* out, int max) {
    for (int i = 0; i < VFS_COUNT; i++) {
        const char* a = name; const char* b = vfs_files[i].name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) {
            int k = 0;
            for (; vfs_files[i].content[k] && k < max - 1; k++) out[k] = vfs_files[i].content[k];
            out[k] = 0;
            return 1;
        }
    }
    return 0;
}

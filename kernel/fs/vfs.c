/*  KengaOS — VFS + initrd (ustar tar) парсер.
    Формат ustar: 512-байтные заголовки + данные.

    Структура заголовка ustar (первые 512 байт файла):
      offset 0:   name[100]
      offset 100: mode[8]       (octal)
      offset 108: uid[8]
      offset 116: gid[8]
      offset 124: size[12]      (octal)
      offset 136: mtime[12]
      offset 148: chksum[8]
      offset 156: typeflag[1]   ('0' = file, '5' = dir)
      offset 157: linkname[100]
      offset 257: magic[6]      "ustar\0"
      offset 263: version[2]
      ...
*/
#include "vfs.h"
#include "../lib/libc.h"
#include "../arch/x86_64/limine.h"

static struct vfs_file files[VFS_MAX_FILES];
static u64 file_count = 0;

static struct vfs_fd fds[VFS_MAX_OPEN];

static u8 *initrd_base = NULL;
static u64 initrd_size = 0;

/* Парсинг octal-строки в число */
static u64 parse_octal(const char *s, u64 len) {
    u64 v = 0;
    for (u64 i = 0; i < len && s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '7') {
            v = (v << 3) | (s[i] - '0');
        }
    }
    return v;
}

void vfs_init(void) {
    file_count = 0;
    for (int i = 0; i < VFS_MAX_OPEN; i++) {
        fds[i].used = false;
    }

    struct limine_module_response *mr = limine_module_request.response;
    if (!mr || mr->module_count == 0) {
        return;
    }

    /* Берём первый модуль как initrd. */
    struct limine_file *mod = mr->modules[0];
    initrd_base = (u8*)mod->address;
    initrd_size = mod->size;

    /* Парсим ustar заголовки. */
    u64 offset = 0;
    while (offset + 512 <= initrd_size && file_count < VFS_MAX_FILES) {
        u8 *header = initrd_base + offset;

        /* Проверка magic "ustar" */
        if (kmemcmp(header + 257, "ustar", 5) != 0) {
            /* Если magic нет — это конец архива или невалидный tar */
            if (header[0] == 0) break;
            offset += 512;
            continue;
        }

        /* typeflag */
        char type = (char)header[156];

        /* name */
        char *name = (char*)header;
        if (name[0] == 0) break;

        /* size */
        u64 size = parse_octal((char*)header + 124, 12);

        if (type == '0' || type == 0) {
            /* Обычный файл */
            struct vfs_file *f = &files[file_count++];
            u64 name_len = kstrlen(name);
            if (name_len >= VFS_PATH_MAX) name_len = VFS_PATH_MAX - 1;
            kmemcpy(f->path, name, name_len);
            f->path[name_len] = 0;
            f->size = size;
            f->data = initrd_base + offset + 512;
        }
        /* type '5' = директория — пропускаем */

        /* Следующий заголовок: выровнен на 512 байт */
        u64 data_size = (size + 511) & ~((u64)511);
        offset += 512 + data_size;
    }
}

i64 vfs_open(const char *path) {
    /* Найти файл */
    struct vfs_file *f = NULL;
    for (u64 i = 0; i < file_count; i++) {
        if (kstrcmp(files[i].path, path) == 0) {
            f = &files[i];
            break;
        }
    }
    if (!f) return -1;

    /* Найти свободный fd */
    for (int i = 0; i < VFS_MAX_OPEN; i++) {
        if (!fds[i].used) {
            fds[i].used = true;
            fds[i].file = f;
            fds[i].pos = 0;
            return (i64)i;
        }
    }
    return -1;
}

i64 vfs_read(i64 fd, void *buf, u64 count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN) return -1;
    if (!fds[fd].used) return -1;
    struct vfs_fd *f = &fds[fd];
    u64 avail = f->file->size - f->pos;
    if (avail == 0) return 0;
    if (count > avail) count = avail;
    kmemcpy(buf, f->file->data + f->pos, count);
    f->pos += count;
    return (i64)count;
}

void vfs_close(i64 fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN) return;
    fds[fd].used = false;
}

u64 vfs_list(struct vfs_file **out, u64 max) {
    u64 n = file_count;
    if (n > max) n = max;
    for (u64 i = 0; i < n; i++) {
        out[i] = &files[i];
    }
    return n;
}

struct vfs_file *vfs_get(u64 index) {
    if (index >= file_count) return NULL;
    return &files[index];
}

u64 vfs_count(void) {
    return file_count;
}

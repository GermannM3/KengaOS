/*  KengaOS — VFS (Virtual File System).
    Простой слой поверх initrd (tar-архива).

    Поддерживается:
      - vfs_open(path) → file descriptor
      - vfs_read(fd, buf, count)
      - vfs_close(fd)
      - vfs_list(dir) → массив имён файлов

    Не поддерживается:
      - Запись (read-only initrd)
      - Real file system (FAT32/EXT2) — это v0.0.5+
      - Директории как таковые — все файлы в корне initrd

    Initrd загружается Limine как module. Это обычный tar-архив
    (ustar формат), который ядро парсит в RAM.
*/
#ifndef KENGA_VFS_H
#define KENGA_VFS_H

#include "../lib/types.h"

#define VFS_MAX_FILES 64
#define VFS_PATH_MAX  256
#define VFS_MAX_OPEN  16

struct vfs_file {
    char path[VFS_PATH_MAX];
    u64  size;
    u8  *data;       /* указатель в RAM (initrd) */
};

struct vfs_fd {
    bool used;
    struct vfs_file *file;
    u64 pos;
};

/* Инициализировать VFS из Limine module (initrd). */
void vfs_init(void);

/* Открыть файл по пути. Возвращает fd (>= 0) или -1 если не найден. */
i64 vfs_open(const char *path);

/* Читать из fd. Возвращает количество прочитанных байт. */
i64 vfs_read(i64 fd, void *buf, u64 count);

/* Закрыть fd. */
void vfs_close(i64 fd);

/* Список файлов (для команды ls). Возвращает количество. */
u64 vfs_list(struct vfs_file **out, u64 max);

/* Найти файл по индексу в массиве (для отладки). */
struct vfs_file *vfs_get(u64 index);

/* Сколько всего файлов в initrd. */
u64 vfs_count(void);

#endif

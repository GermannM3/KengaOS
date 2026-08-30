/*  KengaOS — FAT32 (read/write) поверх AHCI.
    Superfloppy-образ: BPB в секторе 0, без MBR-партиций.
    v1: 8.3 имена (LFN проглатываются), файлы в корне.
*/
#ifndef KENGA_FAT32_H
#define KENGA_FAT32_H

#include "../lib/types.h"

/* Монтировать (проверка BPB + чтение root). 1 = OK. */
int  fat32_init(void);

/* Список файлов корня (печать через kprintf). Возвращает количество. */
u32  fat32_list(void);

/* Читать весь файл в buf (до max байт). <0 = ошибка. */
i32  fat32_read(const char *name, u8 *buf, u32 max);

/* Создать/перезаписать файл данными, вернуть размер (<0 = ошибка). */
i32  fat32_write(const char *name, const u8 *data, u32 len);

#endif

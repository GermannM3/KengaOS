/*  KengaOS — FAT32 поверх AHCI (write/read, superfloppy).
    Опорные точки: BPB в 0 секторе, корень — кластер из BPB+44.
    FAT entry — 28 бит по смещению cl*4.
    v1: 8.3 имена, проглатываем LFN, файлы в корне, кластеры ≤1M.
*/
#include "fat32.h"
#include "../drivers/ahci.h"
#include "../drivers/pioide.h"
#include "../lib/libc.h"
#include "../lib/libc.h"
#include "../drivers/uart.h"
static const char CRLF[3] = {13, 10, 0};

static u32  bytes_per_sector;
static u32  sectors_per_cluster;
static u32  reserved_sectors;
static u32  num_fats;
static u32  fat_size;
static u32  root_cluster;
static u32  first_data_sector;
static int  mounted = 0;

static u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 rd32(const u8 *p) { return rd16(p) | ((u32)rd16(p + 2) << 16); }
static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { wr16(p, v); wr16(p + 2, v >> 16); }

/* Буферы (статические, малые: 512 + 8М? нет — по одному сектору и каталог). */
static u8 sect[512];
static u8 fatbuf[4096];   /* 8 секторов */
static u8 dirbuf[4096];

static u32 sector_of_cluster(u32 cl) {
    return first_data_sector + (cl - 2) * sectors_per_cluster;
}

static int drv_sectors(void) {
    if (ahci_sectors()) return (int)ahci_sectors();
    return (int)pioide_sectors();
}
static int read_sectors(u32 lba, u32 n, void *buf) {
    if (n > 8) n = 8;
    if (ahci_sectors()) return ahci_read(lba, (u16)n, buf);
    return pioide_read(lba, (u16)n, buf);
}

static int write_sectors(u32 lba, u32 n, const void *buf) {
    if (n > 8) n = 8;
    if (ahci_sectors()) return ahci_write(lba, (u16)n, buf);
    return pioide_write(lba, (u16)n, buf);
}

/* FAT-запись кластера. 1 = успех. */
static u32 fat_get(u32 cl) {
    if (!mounted) return 0x0FFFFFF8;
    u32 off = cl * 4;
    u32 lba = reserved_sectors + off / bytes_per_sector;
    u32 idx = (off % bytes_per_sector) / 4;
    if (read_sectors(lba, 1, sect)) return 0;   /* ошибка чтения */
    return rd32(sect + idx * 4) & 0x0FFFFFFF;
}

static int fat_set(u32 cl, u32 val) {
    u32 off = cl * 4;
    u32 lba = reserved_sectors + off / bytes_per_sector;
    u32 idx = (off % bytes_per_sector) / 4;
    if (read_sectors(lba, 1, sect)) return -1;
    u32 v = (rd32(sect + idx * 4) & 0xF0000000) | (val & 0x0FFFFFFF);
    wr32(sect + idx * 4, v);
    return write_sectors(lba, 1, sect) ? -1 : 0;
}

static int is_eoc(u32 v) { return v >= 0x0FFFFFF8; }
static int is_free(u32 v) { return v == 0; }

/* Найти свободный кластер (с FAT2 синхронизацией не заморачиваемся — v1). */
static u32 alloc_cluster(void) {
    for (u32 cl = 2; cl < 0x0FFFFFF0; cl++) {
        u32 v = fat_get(cl);
        if (is_free(v)) return cl;
    }
    return 0;
}

/* Чтение кластера целиком в dirbuf/буфер. */
static int read_cluster(u32 cl, u8 *buf) {
    u32 lba = sector_of_cluster(cl);
    for (u32 i = 0; i < sectors_per_cluster; i += 8) {
        int n = (sectors_per_cluster - i > 8) ? 8 : (int)(sectors_per_cluster - i);
        if (read_sectors(lba + i, n, buf + i * bytes_per_sector)) return -1;
    }
    return 0;
}

static int write_cluster(u32 cl, const u8 *buf) {
    u32 lba = sector_of_cluster(cl);
    for (u32 i = 0; i < sectors_per_cluster; i += 8) {
        int n = (sectors_per_cluster - i > 8) ? 8 : (int)(sectors_per_cluster - i);
        if (write_sectors(lba + i, n, buf + i * bytes_per_sector)) return -1;
    }
    return 0;
}

/* Кластеры директории: цепочка, по kdirbuf. Ошибка/<0. */
static void dir_load(u32 cl, u8 *buf, u32 *out_cl, u32 *out_len) {
    u32 len = 0;
    for (;;) {
        if (read_cluster(cl, buf + len * bytes_per_sector)) { *out_cl = 0; return; }
        len += sectors_per_cluster;
        u32 next = fat_get(cl);
        if (is_eoc(next)) break;
        if (!next || len > 64) { *out_cl = 0; return; }
        cl = next;
    }
    *out_cl = cl;
    *out_len = len;
    (void)out_len;
}

/* Имя 8.3 → "name.ext" и наоборот. Ищем запись в корневой директории.
   ВАЖНО: dirbuf содержит корневую директорию (по концам цепочки). */
static u32 root_load(u8 *buf, u32 *maxclusters) {
    u32 cl = root_cluster;
    u32 len = 0;
    for (u32 guard = 0; guard < 4096; guard++) {
        if (read_cluster(cl, buf + len * bytes_per_sector)) return 0;
        len += sectors_per_cluster;
        u32 next = fat_get(cl);
        if (is_eoc(next)) { *maxclusters = len / bytes_per_sector; return cl; }
        if (!next || len > bytes_per_sector * 64) return 0;
        cl = next;
    }
    return 0;
}

static int name_8_3(const char *name, u8 *nm) {
    /* "FILE.TXT" → "FILE    TXT" (11 байт, пробелы) */
    for (int i = 0; i < 11; i++) nm[i] = ' ';
    int i = 0, j = 0;
    while (name[i] && name[i] != '.') { if (j < 8) nm[j++] = (u8)name[i]; i++; }
    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && j < 11) nm[j++] = (u8)name[i++];
    }
    if (name[i]) return -1;   /* в имени есть непонятные хвосты */
    return 0;
}

static u32 entry_cluster(const u8 *e) {
    return rd16(e + 26) | ((u32)rd16(e + 20) << 16);
}

/* Найти запись по имени. 1 = найдено (в dirbuf + index out). */
static u32 find_entry(const char *name, u8 *nmout, u8 **entry, u32 *entry_lba) {
    u32 nclusters = 0;
    u32 send = root_load(dirbuf, &nclusters);
    if (!send) return 0;
    for (u32 off = 0; off < nclusters * bytes_per_sector; off += 32) {
        u8 e = dirbuf[off];
        if (e == 0x00) break;               /* конец каталога */
        if (e == 0xE5) continue;            /* удалённая */
        if (dirbuf[off + 11] == 0x0F) continue;  /* LFN — в v1 не собираем */
        u8 nm[11];
        for (int i = 0; i < 11; i++) nm[i] = dirbuf[off + i];
        if (nm[11 - 1] & 0x18) continue;    /* LFN-атрибуты уже отсортированы, но перестрахуемся */
        /* сравнение с целевым 8.3 */
        u8 target[11];
        if (name_8_3(name, target)) continue;
        if (kmemcmp(nm, target, 11) == 0) {
            if (nmout) kmemcpy(nmout, nm, 11);
            if (entry) *entry = &dirbuf[off];
            if (entry_lba) *entry_lba = 0;
            return 1;
        }
    }
    return 0;
}

int fat32_init(void) {
    if (drv_sectors() == 0) return 0;
    if (read_sectors(0, 1, sect)) return 0;
    {
        u32 bps = rd16(sect + 11);
        uart_puts(UART_COM1, "fat32: bps=");
        char bb[8]; u32 v = bps, t = 0; char tmp2[8];
        if (!v) tmp2[t++] = 48;
        while (v) { tmp2[t++] = 48 + v % 10; v /= 10; }
        for (u32 i = 0; i < t; i++) bb[i] = tmp2[t - 1 - i];
        bb[t] = 0;
        uart_puts(UART_COM1, bb);
        uart_puts(UART_COM1, " blob=");
        for (int i = 0; i < 16; i++) {
            uart_putc(UART_COM1, "0123456789ABCDEF"[sect[i] >> 4]);
            uart_putc(UART_COM1, "0123456789ABCDEF"[sect[i] & 0xF]);
            uart_putc(UART_COM1, ' ');
        }
        uart_puts(UART_COM1, CRLF);
        if (bps != 512) return 0;
    }
    bytes_per_sector = 512;
    sectors_per_cluster = sect[13];
    reserved_sectors = rd16(sect + 14);
    num_fats = sect[16];
    fat_size = rd32(sect + 36);        /* FAT32 */
    root_cluster = rd32(sect + 44);
    if (!fat_size || !sectors_per_cluster || !reserved_sectors || !root_cluster) return 0;
    first_data_sector = reserved_sectors + num_fats * fat_size;
    /* проверить подпись сектора */
    if (sect[510] != 0x55 || sect[511] != 0xAA) {
        uart_puts(UART_COM1, "[fat32] нет сигнатуры (не FAT32?)\r\n");
        return 0;
    }
    mounted = 1;
    uart_puts(UART_COM1, "[fat32] смонтирован, root_cl=");
    char b[12]; u32 v = root_cluster, t = 0; char tmp[12];
    if (!v) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + v % 10; v /= 10; }
    for (u32 i = 0; i < t; i++) b[i] = tmp[t - 1 - i];
    b[t] = 0;
    uart_puts(UART_COM1, b);
    uart_puts(UART_COM1, "\r\n");
    return 1;
}

u32 fat32_list(void) {
    if (!mounted) return 0;
    u32 nclusters = 0;
    if (!root_load(dirbuf, &nclusters)) return 0;
    u32 count = 0;
    for (u32 off = 0; off < nclusters * bytes_per_sector; off += 32) {
        u8 e = dirbuf[off];
        if (e == 0x00) break;
        if (e == 0xE5 || dirbuf[off + 11] == 0x0F) continue;
        char name[13];
        for (int i = 0; i < 8 && dirbuf[off + i] != ' '; i++) name[i] = dirbuf[off + i];
        u32 l = 0;
        while (l < 8 && name[l]) l++;
        if (dirbuf[off + 8] != ' ') { name[l++] = '.'; for (int i = 0; i < 3 && dirbuf[off + 8 + i] != ' '; i++) name[l++] = dirbuf[off + 8 + i]; }
        name[l] = 0;
        u32 sz = rd32(dirbuf + off + 28);
        kprintf("  %s  %u B\r\n", name, sz);
        count++;
    }
    return count;
}

i32 fat32_read(const char *name, u8 *buf, u32 max) {
    if (!mounted) return -1;
    u32 nclusters = 0;
    if (!root_load(dirbuf, &nclusters)) return -2;
    for (u32 off = 0; off < nclusters * bytes_per_sector; off += 32) {
        if (dirbuf[off] == 0x00) break;
        if (dirbuf[off] == 0xE5 || dirbuf[off + 11] == 0x0F) continue;
        u8 target[11];
        if (name_8_3(name, target)) continue;
        if (kmemcmp(dirbuf + off, target, 11)) continue;
        u32 cl = entry_cluster(dirbuf + off);
        u32 size = rd32(dirbuf + off + 28);
        if (size > max) size = max;
        u32 done = 0;
        while (done < size) {
            if (read_cluster(cl, buf + done)) return -3;
            done += sectors_per_cluster * bytes_per_sector;
            u32 next = fat_get(cl);
            if (is_eoc(next)) break;
            if (!next) return -4;
            cl = next;
        }
        return (i32)size;
    }
    return -5;   /* не найден */
}

i32 fat32_write(const char *name, const u8 *data, u32 len) {
    if (!mounted || len == 0) return -1;
    /* 1) освободить старые кластеры, если файл существует (v1: просто маркируем
       до записи — не удаляем старые; перезапись поверх = новый файл с новым именем?) */
    /* проще: создать/заменить запись — найдём слот в корне */
    u32 nclusters = 0;
    if (!root_load(dirbuf, &nclusters)) return -2;

    u32 clusters_needed = (len + sectors_per_cluster * bytes_per_sector - 1) /
                          (sectors_per_cluster * bytes_per_sector);
    /* 2) выделить кластеры */
    u32 *chain = (u32 *)fatbuf;   /* reuse: не больше 1024 кластеров (4KB) */
    if (clusters_needed > 1024) return -3;
    u32 base_cl = 0;
    for (u32 c = 0; c < clusters_needed; c++) {
        u32 cl = alloc_cluster();
        if (!cl) { return -4; }
        chain[c] = cl;
        if (c == 0) base_cl = cl;
        if (c + 1 < clusters_needed) fat_set(cl, chain[c + 1]);
        else fat_set(cl, 0x0FFFFFFF);
    }
    /* 3) записать данные */
    u32 done = 0;
    for (u32 c = 0; c < clusters_needed && done < len; c++) {
        u32 cl = chain[c];
        u32 chunk = sectors_per_cluster * bytes_per_sector;
        if (done + chunk > len) chunk = len - done;
        kmemset(dirbuf, 0, chunk);
        kmemcpy(dirbuf, data + done, chunk);
        if (write_cluster(cl, dirbuf)) return -5;
        done += chunk;
    }
    /* 4) создать/обновить entry в корне */
    u8 target[11];
    if (name_8_3(name, target)) return -6;
    u32 slot = 0;
    int found_slot = 0;
    u32 send = 0;
    for (u32 off = 0; off < nclusters * bytes_per_sector; off += 32) {
        u8 e = dirbuf[off];
        if (e == 0x00 || e == 0xE5) {
            if (!found_slot) { slot = off; found_slot = 1; }
            if (e == 0x00) break;
        }
    }
    if (!found_slot) { return -7; }  /* корень полон — v1 */
    u8 *ent = dirbuf + slot;
    for (int i = 0; i < 32; i++) ent[i] = 0;
    kmemcpy(ent, target, 11);
    ent[11] = 0x20;               /* attr: archive */
    wr16(ent + 20, (u32)base_cl >> 16);
    wr16(ent + 26, base_cl & 0xFFFF);
    wr32(ent + 28, len);
    /* записать всю загруженную ветку корня: восстановим цепочку кластеров */
    {
        u32 cl = root_cluster;
        u32 written = 0;
        while (written < nclusters * bytes_per_sector) {
            u32 chunk = sectors_per_cluster * bytes_per_sector;
            if (write_cluster(cl, dirbuf + written)) return -8;
            written += chunk;
            u32 next = fat_get(cl);
            if (is_eoc(next)) break;
            if (!next) break;
            cl = next;
        }
    }
    (void)send;
    return (i32)len;
}

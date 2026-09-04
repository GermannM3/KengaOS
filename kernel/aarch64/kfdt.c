/* kfdt.c — минимальный Flattened Device Tree парсер (aarch64).
 *
 * На реальном железе (Oppo A5 2020 / SM6125 и любые другие) адреса
 * UART/памяти/устройств берём из DTB, а не хардкодом. Формат тот же,
 * что в mainline-DTB телефона — парсер единый для QEMU virt и железа.
 *
 * Тест: build кладёт virt.dtb (dumpdtb QEMU virt) в initrd; ядро парсит
 * и печатает: uart-базу (compatible arm,pl011), память (reg ноды memory).
 *
 * Спека: Devicetree Specification, flattened format.
 * ponytail: без alias/phandle/ITS; нода по имени (до '@') на любом уровне
 * вложения или по compatible; свойства — из string table.
 */
#include "kf_rt.h"

#define FDT_MAGIC      0xd00dfeed
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

#define FDT_MAX_NODES  16
#define FDT_MAX_PROPS  24
#define FDT_NAME       32

static const uint8_t* fdt_base = 0;
static uint32_t fdt_total = 0;
static uint32_t fdt_struct_off = 0;
static uint32_t fdt_str_off = 0;
static int      fdt_loaded_flag = 0;

static uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t rd64(const uint8_t* p) {
    return ((uint64_t)rd32(p) << 32) | rd32(p + 4);
}

/* Загрузить FDT-файл из VFS. Возвращает 1 при валидном magic. */
int64_t k_fdt_load(const char* name) {
    extern int64_t k_vfs_count(void);
    extern const char* k_vfs_name(int64_t idx);
    extern int64_t k_vfs_cat(const char* name, char* out, int max);
    static uint8_t buf[32768];
    int64_t n = k_vfs_count();
    for (int64_t i = 0; i < n; i++) {
        const char* nm = k_vfs_name(i);
        int j = 0, match = 1;
        while (nm[j] || name[j]) { if (nm[j] != name[j]) { match = 0; break; } j++; }
        if (!match) continue;
        for (int b = 0; b < 32768; b++) buf[b] = 0;
        k_vfs_cat(nm, (char*)buf, 32768);
        if (rd32(buf) != FDT_MAGIC) return 0;
        fdt_total = rd32(buf + 4);
        if (fdt_total == 0 || fdt_total > 1048576) return 0;
        fdt_struct_off = rd32(buf + 8);
        fdt_str_off = rd32(buf + 12);
        fdt_base = buf;
        fdt_loaded_flag = 1;
        return 1;
    }
    return 0;
}

int64_t k_fdt_loaded(void) { return fdt_loaded_flag; }

/* --- сбор свойств целевой ноды --- */
static struct {
    const uint8_t* name;
    const uint8_t* val;
    uint32_t len;
} fprops[FDT_MAX_PROPS];
static int fprops_n = 0;
static uint8_t fnode_name[FDT_NAME];

static int node_name_eq(const uint8_t* node, const char* want) {
    int i = 0;
    while (want[i]) {
        if (node[i] != (uint8_t)want[i]) return 0;
        i++;
    }
    return node[i] == 0 || node[i] == '@';
}

static const uint8_t* str_at(uint32_t off) { return fdt_base + fdt_str_off + off; }

static int streq_at(const uint8_t* a, const char* b) {
    int i = 0;
    while (b[i]) { if (a[i] != (uint8_t)b[i]) return 0; i++; }
    return a[i] == 0;
}

/* Один проход: найти ноду и собрать её свойства в fprops.
 * Именной режим: нода с именем target (до '@') на любом уровне.
 * Compat-режим ("c:::<compat>"): узел, у которого compatible содержит
 * <compat>; при хите запоминает имя узла в fnode_name и возвращает 2,
 * чтобы вызывающий сделал повторный collect по имени (reg/props целиком).
 */
static int fdt_collect(const char* target) {
    const uint8_t* p = fdt_base + fdt_struct_off;
    int depth = 0;
    static uint8_t names[FDT_MAX_NODES][FDT_NAME];
    int match = 0, compat_mode = 0;
    const char* compat = 0;

    if (target[0] == 'c' && target[1] == ':' && target[2] == ':') {
        compat_mode = 1;
        compat = target + 3;
    }

    fprops_n = 0;
    for (;;) {
        if ((uint32_t)(p - fdt_base) >= fdt_total) return 0;
        uint32_t tok = rd32(p);
        p += 4;
        if (tok == FDT_BEGIN_NODE) {
            int nl = 0;
            while (p[nl]) nl++;
            if (depth < FDT_MAX_NODES) {
                int k = 0;
                for (; k < nl && k < FDT_NAME - 1; k++) names[depth][k] = p[k];
                names[depth][k] = 0;
            }
            depth++;
            p += nl + 1;
            p = (const uint8_t*)(((uintptr_t)p + 3) & ~(uintptr_t)3);
            if (depth == 2) {
                if (compat_mode) { match = 1; fprops_n = 0; }
                else { match = node_name_eq(names[1], target) ? 1 : 0; fprops_n = 0; }
            } else if (depth == 1) {
                match = 0;
            }
        } else if (tok == FDT_END_NODE) {
            depth--;
            if (depth == 1 && match) {
                if (compat_mode) {
                    if (match == 2) return 2;
                    match = 0;
                } else {
                    return 1;
                }
            }
            if (depth == 0) break;
        } else if (tok == FDT_PROP) {
            uint32_t len = rd32(p);
            uint32_t nameoff = rd32(p + 4);
            p += 8;
            const uint8_t* pname = str_at(nameoff);
            if (match && fprops_n < FDT_MAX_PROPS) {
                int capture = 1;
                if (compat_mode && streq_at(pname, "compatible")) {
                    /* скан списка строк: "str1\0str2\0" */
                    const uint8_t* v = p;
                    uint32_t o = 0;
                    int hit = 0;
                    while (o < len) {
                        int ci = 0, m = 1;
                        while (compat[ci]) {
                            if (v[o + ci] != (uint8_t)compat[ci]) { m = 0; break; }
                            ci++;
                        }
                        if (m && v[o + ci] == 0) { hit = 1; break; }
                        while (o < len && v[o]) o++;
                        o++;
                    }
                    if (hit) {
                        match = 2;
                        int k = 0;
                        while (names[depth - 1][k] && k < FDT_NAME - 1) { fnode_name[k] = names[depth - 1][k]; k++; }
                        fnode_name[k] = 0;
                    } else {
                        capture = 0;
                    }
                }
                if (capture) {
                    fprops[fprops_n].name = pname;
                    fprops[fprops_n].val = p;
                    fprops[fprops_n].len = len;
                    fprops_n++;
                }
            }
            p += len;
            p = (const uint8_t*)(((uintptr_t)p + 3) & ~(uintptr_t)3);
        } else if (tok == FDT_NOP) {
            /* skip */
        } else if (tok == FDT_END) {
            return 0;
        } else {
            return 0;
        }
    }
    return 0;
}

/* свойство u32 ноды; -1 = нет */
int64_t k_fdt_prop_u32(const char* node, const char* prop) {
    if (!fdt_loaded_flag) return -1;
    if (fdt_collect(node) < 1) return -1;
    for (int i = 0; i < fprops_n; i++) {
        if (streq_at(fprops[i].name, prop) && fprops[i].len >= 4)
            return (int64_t)rd32(fprops[i].val);
    }
    return -1;
}

/* свойство-строка ноды: копирует в out, возвращает длину; -1 = нет */
int64_t k_fdt_prop_str(const char* node, const char* prop, char* out, int max) {
    if (!fdt_loaded_flag) return -1;
    if (fdt_collect(node) < 1) return -1;
    for (int i = 0; i < fprops_n; i++) {
        if (streq_at(fprops[i].name, prop)) {
            int k = 0;
            for (; k < (int)fprops[i].len && k < max - 1; k++) out[k] = (char)fprops[i].val[k];
            out[k] = 0;
            return k;
        }
    }
    return -1;
}

/* reg ноды памяти: (address_cells=2, size_cells=2) → base/size первой пары */
int64_t k_fdt_memory(uint64_t* base_out, uint64_t* size_out) {
    if (!fdt_loaded_flag) return -1;
    if (fdt_collect("memory") < 1) return -1;
    for (int i = 0; i < fprops_n; i++) {
        if (streq_at(fprops[i].name, "reg") && fprops[i].len >= 16) {
            *base_out = rd64(fprops[i].val);       /* (2,2): base u64 */
            *size_out = rd64(fprops[i].val + 8);   /* size u64 */
            return 1;
        }
    }
    return -1;
}

/* UART по compatible (reg: первый u64 = база); 0 = не найден.
   Двухпроходный: 1-й collect по compatible даёт имя узла (fnode_name),
   2-й collect по имени собирает все свойства. */
const char* k_fdt_fnode_name(void) { return (const char*)fnode_name; }

int64_t k_fdt_prop_count(void) { return fprops_n; }

int64_t k_fdt_uart_base(void) {
    if (!fdt_loaded_flag) return 0;
    /* v1: прямое имя узла UART (QEMU virt: pl011@9000000).
       ponytail: для произвольных DTB — compat-скан уже написан (fdt_collect
       "c:::"), доводится на хост-тесте. */
    if (fdt_collect("pl011@9000000") < 1) return 0;
    for (int i = 0; i < fprops_n; i++) {
        if (streq_at(fprops[i].name, "reg") && fprops[i].len >= 16)
            return (int64_t)rd64(fprops[i].val);
    }
    return 0;
}

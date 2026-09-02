/* kf_pkg.c — пакетная система v1 (kpkg).
 *
 * Формат .kpkg v1 — текстовый манифест в initrd:
 *   name=Калькулятор
 *   version=1.0.0
 *   desc=Считает через модель-агента
 *   entry=calc
 *
 * v1: пакеты лежат в initrd рядом с ядром; "установка" — регистрация
 * в реестре + событие в лог UI. Полноценная установка (копирование на
 * диск, ring-3 запуск) — этап user-mode.
 * ponytail: max 8 пакетов, строки в статике — апгрейд вместе с VFS-записью.
 */
#include "kf_rt.h"

/* --- VFS (kf_vfs.c) --- */
extern int64_t k_vfs_count(void);
extern const char* k_vfs_name(int64_t idx);
extern int64_t k_vfs_cat(const char* name, char* out, int max);

#define PKG_MAX     8
#define PKG_STR     48
#define PKG_RAW_MAX 256

typedef struct {
    char file[32];
    char name[PKG_STR];
    char ver[16];
    char desc[PKG_STR];
    char entry[24];
    int  installed;
} kpkg;

static kpkg  pkgs[PKG_MAX];
static int   pkg_count = 0;
static int   pkg_inited = 0;

static void pstr_copy(char* dst, int dn, const char* src, const char* key, int klen) {
    /* src указывает на значение после "key="; копируем до \n */
    int n = 0;
    while (src[n] && src[n] != '\n' && src[n] != '\r' && n < dn - 1) { dst[n] = src[n]; n++; }
    dst[n] = 0;
    (void)key; (void)klen;
}

static int parse_kpkg(const char* text, kpkg* p) {
    /* жёсткая структура v1: имя файла даёт file=, поля ищем по префиксам */
    const char* keys[4] = { "name=", "version=", "desc=", "entry=" };
    char* dsts[4] = { p->name, p->ver, p->desc, p->entry };
    int dns[4] = { PKG_STR, 16, PKG_STR, 24 };
    int found = 0;
    for (int k = 0; k < 4; k++) {
        const char* t = text;
        int klen = 0;
        while (keys[k][klen]) klen++;
        int got = 0;
        while (*t) {
            int i = 0;
            while (i < klen && t[i] == keys[k][i]) i++;
            if (i == klen) { pstr_copy(dsts[k], dns[k], t + klen, keys[k], klen); got = 1; break; }
            while (*t && *t != '\n') t++;
            if (*t) t++;
        }
        if (!got) return 0;
        found++;
    }
    return found == 4;
}

int64_t k_pkg_init(void) {
    if (pkg_inited) return 1;
    int64_t n = k_vfs_count();
    static char raw[PKG_RAW_MAX];
    for (int64_t i = 0; i < n && pkg_count < PKG_MAX; i++) {
        const char* nm = k_vfs_name(i);
        int ln = 0;
        while (nm[ln]) ln++;
        if (ln > 5 && nm[ln-5] == '.' && nm[ln-4] == 'k' && nm[ln-3] == 'p' && nm[ln-2] == 'k' && nm[ln-1] == 'g') {
            for (int j = 0; j < 31 && j < ln; j++) pkgs[pkg_count].file[j] = nm[j];
            pkgs[pkg_count].file[31] = 0;
            for (int j = 0; j < PKG_RAW_MAX; j++) raw[j] = 0;
            k_vfs_cat(nm, raw, PKG_RAW_MAX);
            if (parse_kpkg(raw, &pkgs[pkg_count])) {
                pkgs[pkg_count].installed = 0;
                pkg_count++;
            }
        }
    }
    pkg_inited = 1;
    return pkg_count;
}

int64_t k_pkg_count(void)        { return pkg_count; }
const char* k_pkg_file(int64_t i)  { return (i >= 0 && i < pkg_count) ? pkgs[i].file : ""; }
const char* k_pkg_name(int64_t i)  { return (i >= 0 && i < pkg_count) ? pkgs[i].name : ""; }
const char* k_pkg_ver(int64_t i)   { return (i >= 0 && i < pkg_count) ? pkgs[i].ver : ""; }
const char* k_pkg_desc(int64_t i)  { return (i >= 0 && i < pkg_count) ? pkgs[i].desc : ""; }
const char* k_pkg_entry(int64_t i) { return (i >= 0 && i < pkg_count) ? pkgs[i].entry : ""; }
int64_t k_pkg_installed(int64_t i){ return (i >= 0 && i < pkg_count) ? pkgs[i].installed : -1; }

/* v1: установка = регистрация. v2: копирование в /apps на диск + запуск. */
int64_t k_pkg_install(int64_t i) {
    if (i < 0 || i >= pkg_count) return 0;
    pkgs[i].installed = 1;
    return 1;
}

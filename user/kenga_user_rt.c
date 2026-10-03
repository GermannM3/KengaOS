/* kenga_user_rt.c — ring-3 runtime-клей для Kenga-приложений,
 * собранных emit-c --freestanding (scripts/build-user.sh).
 *
 * Сгенерированный app.c самодостаточен, но рассчитывает на:
 *   - kf_alloc(n)  — weak-hook аллокатора (строки/списки рантайма);
 *   - kenga_print*() — emit-c делает их no-op; скрипт патчит println
 *     в int 0x80 write;
 *   - точку входа _start.
 * В ring 3 нельзя cli/hlt, поэтому k_die() из сгенерированного файла в
 * нормальной работе недостижим: выход — только sys_exit. */
#include <stdint.h>
#include <stddef.h>

/* --- syscalls int 0x80: 1 = write(rdi=buf, rsi=len), 2 = exit --- */
long k_sys_write(const char* s, unsigned long n) {
    long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(1L), "D"(s), "S"(n));
    return r;
}

long k_sys_writeln(const char* s) {
    if (s) {
        unsigned long n = 0;
        while (s[n]) n++;
        k_sys_write(s, n);
    }
    k_sys_write("\n", 1);
    return 0;
}

int64_t k_sys_exit(void) {
    __asm__ __volatile__("int $0x80" : : "a"(2L));
    for (;;) { }        /* sys_exit не возвращается */
    return 0;
}

/* --- bump-аллокатор для freestanding-рантайма Kenga --- */
static unsigned char k_heap[64 * 1024];
static unsigned long k_used = 0;

void* kf_alloc(size_t n) {
    if (n == 0) n = 1;
    n = (n + 15u) & ~(size_t)15u;
    if (k_used + n > sizeof(k_heap)) return 0;
    void* p = (void*)&k_heap[k_used];
    k_used += n;
    return p;
}

/* --- файловый syscall: cat(path, dst, max) -> байты из KengaFS ---
   Файловая система целиком на стороне ядра/Kenga; здесь только int 0x80
   и буфер приложения. */
int64_t k_sys_cat(const char* path, int64_t dst, int64_t max) {
    long r;
    __asm__ __volatile__("int $0x80"
                         : "=a"(r)
                         : "a"(16L), "D"(path), "S"(dst), "d"(max));
    return r;
}

/* --- запись файла: save(path, data, len) -> байты (KengaFS на стороне ядра) --- */
int64_t k_sys_save(const char* path, const char* data, int64_t len) {
    long r;
    __asm__ __volatile__("int $0x80"
                         : "=a"(r)
                         : "a"(17L), "D"(path), "S"(data), "d"(len));
    return r;
}

/* --- многозадачность: добровольно уступить CPU и узнать свой pid --- */
int64_t k_sys_yield(void) {
    long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(3L));
    return r;
}

int64_t k_sys_mkdir(const char* path) {
    long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(18L), "D"(path));
    return r;
}

int64_t k_sys_rm(const char* path) {
    long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(19L), "D"(path));
    return r;
}

int64_t k_sys_getpid(void) {
    long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(4L));
    return r;
}

/* --- буфер приложения для принимаемых данных + приведение адреса к str --- */
static char g_ubuf[4096];

int64_t k_rt_buf(void) { return (int64_t)(uintptr_t)g_ubuf; }
const char* k_rt_str(int64_t addr) { return (const char*)(uintptr_t)addr; }

/* --- точка входа ELF --- */
int64_t main(void);

void _start(void) {
    main();
    k_sys_exit();
}

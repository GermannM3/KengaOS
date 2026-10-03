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

/* --- точка входа ELF --- */
int64_t main(void);

void _start(void) {
    main();
    k_sys_exit();
}

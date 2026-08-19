/* KengaOS — минимальный kernel-аллокатор для FFI-хука kf_alloc.
   RUNTIME_FS (emit-c) вызывает weak kf_alloc() для служебных выделений
   (kstr_index_val и т.п.). Пока планировщик/buddy-аллокатор не готов,
   используем простой bump-аллокатор над статическим буфером в .bss.
   Указатели не освобождаются — для ранней загрузки это допустимо.
*/
#include <stddef.h>
#include <stdint.h>

#define KF_ALLOC_ARENA_SIZE (8192)

static uint8_t  kf_arena[KF_ALLOC_ARENA_SIZE] __attribute__((aligned(16)));
static uint64_t kf_used = 0;

void *kf_alloc(size_t n) {
    if (n == 0) {
        n = 1;
    }
    n = (n + 15u) & ~(size_t)15u;
    if (kf_used + n > sizeof(kf_arena)) {
        return 0; /* OOM: _k_arena_alloc уйдёт в kf_libc_malloc, затем k_die */
    }
    void *p = &kf_arena[kf_used];
    kf_used += n;
    return p;
}
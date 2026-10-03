/* user_a64.c — user-mode (ring 3) для aarch64: этап 2 (ERET + отдельные TTBR).
   Пока заглушки: kmain печатает RING3 SKIP. */
#include "kf_rt.h"

int64_t k_user_boot_test(void)   { return 0; }
int64_t k_user_exec_vfs(const char* name) { (void)name; return 0; }
int64_t k_user_exec_blob(int64_t a, int64_t s) { (void)a; (void)s; return 0; }
int64_t k_user_set_fs(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0;
}
/* Многозадачность ring-3 процессов — пока x86_64-only (kernel/kf_user.c). */
int64_t k_user_spawn_vfs(const char* name) { (void)name; return 0; }
int64_t k_user_spawn_blob(int64_t addr, int64_t size) { (void)addr; (void)size; return 0; }
int64_t k_user_sched_run(void) { return 0; }

int64_t k_user_run(int64_t entry) { (void)entry; return 0; }

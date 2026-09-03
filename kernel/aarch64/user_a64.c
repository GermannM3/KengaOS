/* user_a64.c — user-mode (ring 3) для aarch64: этап 2 (ERET + отдельные TTBR).
   Пока заглушки: kmain печатает RING3 SKIP. */
#include "kf_rt.h"

int64_t k_user_boot_test(void)   { return 0; }
int64_t k_user_exec_vfs(const char* name) { (void)name; return 0; }
int64_t k_user_run(int64_t entry) { (void)entry; return 0; }

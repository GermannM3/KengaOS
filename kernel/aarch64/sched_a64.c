/* sched_a64.c — заглушки планировщика для этапа 1.
 *
 * ponytail: x86 sched.c гоняет context switch на asm (iretq-фреймы) —
 * на a64 это отдельная работа. Агенты Kenga регистрируются и ходят
 * кооперативно (k_task_create = 0 — поток не создаётся, IPC живёт).
 * Апгрейд: port switch.S на aarch64 (cps/sp/elr) — отдельным этапом.
 */
#include "kf_rt.h"

uint64_t k_task_create(void (*entry)(void)) {
    (void)entry;
    return 0;   /* поток не создан — вызывающий это переживает */
}

uint64_t k_task_yield(void) {
    return 0;   /* cooperative: просто продолжаем */
}

uint64_t k_sched_current(void) {
    return 0;
}

int64_t k_sched_task_alive(uint64_t id) {
    (void)id;
    return 1;
}

int64_t k_sched_init(void) { return 1; }

int64_t k_yield_agent(void) { return 0; }   /* x86: k_task_yield() */

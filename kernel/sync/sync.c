/*  KengaOS — Синхронизация.
    Spinlock на атомарном xchg.
    Semaphore на spinlock + busy-wait (пока без wait queue — это v0.0.5+).
*/
#include "sync.h"
#include "../sched/thread.h"
#include "../lib/libc.h"

/* === Атомарные операции (inline) === */

static inline u64 atomic_xchg(volatile u64 *ptr, u64 new_val) {
    u64 old;
    __asm__ volatile (
        "xchgq %0, %1"
        : "=r"(old), "+m"(*ptr)
        : "0"(new_val)
        : "memory"
    );
    return old;
}

static inline bool atomic_cas(volatile u64 *ptr, u64 expected, u64 desired) {
    u64 ret;
    __asm__ volatile (
        "cmpxchgq %2, %1"
        : "=a"(ret), "+m"(*ptr)
        : "r"(desired), "a"(expected)
        : "memory"
    );
    return ret == expected;
}

static inline void cpu_relax(void) {
    __asm__ volatile ("pause" ::: "memory");
}

/* === Spinlock === */

void spinlock_init(struct spinlock *s) {
    s->locked = 0;
    s->holder_thread_id = 0;
}

void spinlock_lock(struct spinlock *s) {
    struct thread *cur = thread_current();
    u64 my_id = cur ? cur->id : 0;

    /* Если уже держим — ошибка (но в v0.0.x упрощаем: просто return). */
    if (s->holder_thread_id == my_id && s->locked) return;

    while (atomic_xchg(&s->locked, 1) != 0) {
        cpu_relax();
    }
    s->holder_thread_id = my_id;
}

void spinlock_unlock(struct spinlock *s) {
    s->holder_thread_id = 0;
    __asm__ volatile ("" ::: "memory");
    s->locked = 0;
}

bool spinlock_try_lock(struct spinlock *s) {
    if (atomic_xchg(&s->locked, 1) != 0) return false;
    struct thread *cur = thread_current();
    s->holder_thread_id = cur ? cur->id : 0;
    return true;
}

/* === Semaphore === */

void semaphore_init(struct semaphore *sem, i64 initial) {
    sem->count = initial;
    spinlock_init(&sem->lock);
}

void semaphore_wait(struct semaphore *sem) {
    for (;;) {
        spinlock_lock(&sem->lock);
        if (sem->count > 0) {
            sem->count--;
            spinlock_unlock(&sem->lock);
            return;
        }
        spinlock_unlock(&sem->lock);
        cpu_relax();
        /* TODO: реально спать, а не busy-wait. Когда появится wait_queue
           в v0.0.5+, заменим. */
    }
}

void semaphore_signal(struct semaphore *sem) {
    spinlock_lock(&sem->lock);
    sem->count++;
    spinlock_unlock(&sem->lock);
}

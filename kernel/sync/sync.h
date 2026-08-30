/*  KengaOS — Синхронизация.
    Spinlock и semaphore для использования между потоками.

    Spinlock: блокирующий, для коротких критических секций.
    Semaphore: считающий, для ресурсов.

    Реализованы через атомарные операции (xchg, cmpxchg).
*/
#ifndef KENGA_SYNC_H
#define KENGA_SYNC_H

#include "../lib/types.h"

/* === Spinlock === */
struct spinlock {
    volatile u64 locked;
    u64 holder_thread_id;   /* для отладки */
};

void spinlock_init(struct spinlock *s);
void spinlock_lock(struct spinlock *s);
void spinlock_unlock(struct spinlock *s);
bool spinlock_try_lock(struct spinlock *s);

/* === Semaphore === */
struct semaphore {
    volatile i64 count;
    struct spinlock lock;
};

void semaphore_init(struct semaphore *sem, i64 initial);
void semaphore_wait(struct semaphore *sem);
void semaphore_signal(struct semaphore *sem);

#endif

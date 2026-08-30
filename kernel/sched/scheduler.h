/*  KengaOS — Кооперативно-вытесняемый round-robin планировщик.

    Модель v0.0.3:
      - kmain (shell) — это "поток #0", крутится в своём контексте.
      - Дополнительные потоки создаются через thread_create + sched_enqueue.
      - Timer IRQ (1000 Гц) каждые TIME_SLICE_MS переключает на следующий поток.
      - Когда потоков больше нет — возвращаемся к kmain.

    Это НЕ полноценный preemptive scheduler:
      - нет приоритетов
      - нет блокировок (mutex/semaphore)
      - нет user-mode (ring 3)
      - нет SMP
    Это базовый слой для v0.0.3, поверх которого в v0.4.x будет
    предиктивный планировщик.
*/
#ifndef KENGA_SCHED_H
#define KENGA_SCHED_H

#include "../lib/types.h"
#include "thread.h"

/* Инициализировать планировщик. */
void sched_init(void);

/* Зарегистрировать kmain как поток #0 (с уже существующим стеком). */
void sched_register_main(void);

/* Добавить поток в очередь. */
void sched_enqueue(struct thread *t);

/* Tick callback — вызывается из IRQ0. */
void sched_tick(void *ctx);

/* Текущий поток. */
extern struct thread *sched_current;

/* Помечает текущий поток как завершённый. */
void sched_mark_current_finished(void);

/* Текущий тик (1 мс). */
u64 sched_ticks(void);

/* Включить планировщик (после этого IRQ0 начнёт переключать потоки). */
void sched_enable(void);

/* Выключить планировщик (для критических секций). */
void sched_disable(void);

#endif

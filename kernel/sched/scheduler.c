/*  KengaOS — Кооперативно-вытесняемый round-robin планировщик.
*/
#include "scheduler.h"
#include "thread.h"
#include "../mem/buddy.h"
#include "../lib/libc.h"
#include "../arch/x86_64/io.h"
#include "../drivers/uart.h"

/* Текущий поток (NULL = kmain / idle). */
struct thread *sched_current = NULL;

/* Поток kmain (статический, без buddy alloc — у него уже есть стек). */
static struct thread main_thread;

/* Кольцевой список READY потоков. */
static struct thread *ready_head = NULL;
static struct thread *ready_tail = NULL;

static u64 tick_count = 0;
static bool scheduler_enabled = false;
static u64 context_switches = 0;

#define TIME_SLICE_TICKS 10   /* 10 мс на поток */

void sched_init(void) {
    sched_current = NULL;
    ready_head = ready_tail = NULL;
    tick_count = 0;
    scheduler_enabled = false;
    context_switches = 0;
    kmemset(&main_thread, 0, sizeof(main_thread));
    main_thread.id = 0;
    kstrncpy(main_thread.name, "kmain", THREAD_NAME_MAX - 1);
    main_thread.state = THREAD_RUNNING;
    /* rsp не имеет значения — kmain не переключается через context_switch,
       он крутится в своей главной функции. */
    sched_current = &main_thread;
}

void sched_register_main(void) {
    /* Уже сделано в sched_init — main_thread это статический слот. */
}

void sched_enqueue(struct thread *t) {
    if (!t) return;
    /* CLI для атомарности. */
    bool was_enabled = scheduler_enabled;
    scheduler_enabled = false;

    t->next = NULL;
    if (!ready_head) {
        ready_head = ready_tail = t;
    } else {
        ready_tail->next = t;
        ready_tail = t;
    }
    t->state = THREAD_READY;

    scheduler_enabled = was_enabled;
}

void sched_mark_current_finished(void) {
    if (sched_current) {
        sched_current->state = THREAD_FINISHED;
    }
}

u64 sched_ticks(void) {
    return tick_count;
}

void sched_enable(void)   { scheduler_enabled = true; }
void sched_disable(void)  { scheduler_enabled = false; }

/* context_switch: переключиться на новый стек.
   Реализация в switch.S.
   Сохраняет callee-saved регистры текущего потока на его стек,
   загружает callee-saved регистры нового потока. */
extern void context_switch(struct thread *from, struct thread *to);

static struct thread *pick_next(void) {
    if (!ready_head) return NULL;
    struct thread *t = ready_head;
    ready_head = ready_head->next;
    if (!ready_head) ready_tail = NULL;
    t->next = NULL;
    return t;
}

void sched_tick(void *ctx) {
    (void)ctx;
    tick_count++;

    if (!scheduler_enabled) return;

    /* Меняем поток только каждые TIME_SLICE_TICKS. */
    if ((tick_count % TIME_SLICE_TICKS) != 0) return;

    /* Если current — это kmain (id==0), и в очереди никого нет —
       остаёмся. Если в очереди есть поток — переключаемся на него. */
    struct thread *cur = sched_current;

    if (cur->state == THREAD_FINISHED) {
        /* Текущий завершён — переключаемся на следующий или на kmain. */
        struct thread *next = pick_next();
        if (next) {
            next->state = THREAD_RUNNING;
            sched_current = next;
            context_switch(cur, next);
            context_switches++;
        } else {
            sched_current = &main_thread;
            main_thread.state = THREAD_RUNNING;
            context_switch(cur, &main_thread);
            context_switches++;
        }
        return;
    }

    /* Текущий ещё жив — проверить, есть ли кому уступить CPU. */
    struct thread *next = pick_next();
    if (!next) return;   /* никто не ждёт — продолжаем */

    /* Поставить текущий обратно в очередь.
       pick_next мог обнулить ready_tail (очередь опустела) —
       нельзя писать в ready_tail->next без проверки. */
    cur->state = THREAD_READY;
    if (cur != &main_thread) {
        /* kmain не возвращаем в очередь — он «special».
           Когда все потоки закончатся, scheduler вернётся к kmain автоматически. */
        if (!ready_head) {
            ready_head = ready_tail = cur;
        } else {
            ready_tail->next = cur;
            ready_tail = cur;
        }
        cur->next = NULL;
    }

    next->state = THREAD_RUNNING;
    sched_current = next;
    context_switch(cur, next);
    context_switches++;
}

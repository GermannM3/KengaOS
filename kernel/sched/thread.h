/*  KengaOS — Thread (lightweight process).
    Один поток = один стек + сохранённые регистры.
    Память для стека выделяется через buddy allocator.
*/
#ifndef KENGA_THREAD_H
#define KENGA_THREAD_H

#include "../lib/types.h"

#define THREAD_NAME_MAX 32
#define THREAD_STACK_PAGES 4   /* 16 KB стек на поток */
#define THREAD_MAX 64          /* максимум потоков */

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_FINISHED
} thread_state_t;

struct thread {
    u64 id;
    char name[THREAD_NAME_MAX];
    thread_state_t state;

    /* Сохранённый контекст (см. switch.S) */
    u64 rsp;          /* стек в момент приостановки */
    u64 rip;          /* точка входа / точка возврата */
    u64 rflags;

    /* Стек потока (физический адрес от buddy allocator) */
    u64 stack_base;   /* начало стека (низ) */
    u64 stack_top;    /* верх стека (высокий адрес) */

    /* Связанный список (round-robin). */
    struct thread *next;
};

/* Создать новый поток. Возвращает NULL при ошибке. */
struct thread *thread_create(const char *name, void (*entry)(void *arg), void *arg);

/* Завершить текущий поток. */
void thread_exit(void);

/* Текущий поток. */
struct thread *thread_current(void);

/* Найти поток по ID. */
struct thread *thread_by_id(u64 id);

/* Список всех потоков (для команды shell). */
u64 thread_enumerate(struct thread **out, u64 max);

#endif

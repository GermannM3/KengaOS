/*  KengaOS — Управление потоками.
    Стек потока выделяется через buddy allocator, инициализируется
    так, чтобы первый context_switch выглядел как возврат из функции.

    Начальный кадр стека (сверху вниз, low addr → high addr):

      thread_trampoline (return addr, кладётся switch.S при ret)
      rflags (fake)
      thread_exit (return addr для trampoline)
      entry (адрес функции потока)
      arg (передаётся в entry через rdi)

    См. switch.S — там thread_trampoline pop'ает entry, arg, и jmp'ает.
*/
#include "thread.h"
#include "../mem/buddy.h"
#include "../lib/libc.h"
#include "../arch/x86_64/io.h"
#include "../drivers/uart.h"
#include "../vmm/vmm.h"

/* Массив всех потоков (без динамической аллокации). */
static struct thread threads_pool[THREAD_MAX];
static u64 next_thread_id = 1;

struct thread *thread_current(void) {
    extern struct thread *sched_current;
    return sched_current;
}

struct thread *thread_by_id(u64 id) {
    for (int i = 0; i < THREAD_MAX; i++) {
        if (threads_pool[i].state != THREAD_UNUSED && threads_pool[i].id == id) {
            return &threads_pool[i];
        }
    }
    return NULL;
}

u64 thread_enumerate(struct thread **out, u64 max) {
    u64 n = 0;
    for (int i = 0; i < THREAD_MAX && n < max; i++) {
        if (threads_pool[i].state != THREAD_UNUSED) {
            out[n++] = &threads_pool[i];
        }
    }
    return n;
}

/* Trampoline — реализован в switch.S. */
extern void thread_trampoline(void);

/* Thread exit — реализована ниже, но не extern. */
void thread_exit(void);

struct thread *thread_create(const char *name, void (*entry)(void *arg), void *arg) {
    /* Найти свободный слот. */
    struct thread *t = NULL;
    for (int i = 0; i < THREAD_MAX; i++) {
        if (threads_pool[i].state == THREAD_UNUSED) {
            t = &threads_pool[i];
            break;
        }
    }
    if (!t) return NULL;

    /* Аллоцировать стек. THREAD_STACK_PAGES = 4 → order = 2 (4 = 2^2). */
    u64 stack_phys = buddy_alloc(2);
    if (stack_phys == 0) return NULL;

    /* Обнулить слот (на всякий случай). */
    kmemset(t, 0, sizeof(*t));

    t->stack_base = stack_phys;
    /* RSP должен быть виртуальным адресом: Limine не identity-mappит RAM,
       доступ к физ. страницам — только через HHDM. */
    t->stack_top  = (u64)phys_to_virt(stack_phys) + (THREAD_STACK_PAGES * 4096);

    kstrncpy(t->name, name ? name : "?", THREAD_NAME_MAX - 1);
    t->name[THREAD_NAME_MAX - 1] = 0;
    t->id = next_thread_id++;
    t->state = THREAD_READY;

    /* Подготовить начальный стек.

       stack_top — kernel-virtual (HHDM), доступен напрямую.

       Стек растёт вниз. sp указывает на последний занятый элемент.

       Layout (сверху вниз, от старшего адреса к младшему):
         [top of stack]
         [arg]                 ← что trampoline положит в rdi
         [entry]               ← куда jmp'нет trampoline
         [thread_exit]         ← return addr для entry
         [thread_trampoline]   ← сюда должен попасть ret из context_switch
         [48 байт запаса]      ← 6 callee-saved pop'ов из context_switch
       ↑ t->rsp указывает сюда

       ВАЖНО: context_switch сначала делает 6 pop'ов (callee-saved),
       потом ret — поэтому t->rsp = слот trampoline − 48, иначе ret
       вытащит мусор (ядро прыгает на 0 → #PF).
       stack_top кратен 4096: stack_top−80 выровнен на 16 байт.
    */

    u64 *sp = (u64 *)t->stack_top;
    sp--; *sp = (u64)arg;                  /* arg */
    sp--; *sp = (u64)entry;                /* entry */
    sp--; *sp = (u64)thread_exit;          /* return для entry */
    sp--; *sp = (u64)thread_trampoline;    /* сюда приходит ret из switch.S */

    t->rsp = (u64)sp - 48;                 /* 6 callee-saved pop'ов */

    /* rip не используется для существующих потоков — только для новых,
       и switch.S делает ret, который берёт адрес с верхушки стека.
       Так что rip можно не задавать. */
    t->rip = (u64)thread_trampoline;
    t->rflags = 0x202;

    return t;
}

void thread_exit(void) {
    extern void sched_mark_current_finished(void);
    sched_mark_current_finished();

    /* После пометки FINISHED планировщик на следующем тике переключится
       на другой поток. А пока — hlt. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

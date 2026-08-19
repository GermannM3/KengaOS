/* sched.c — cooperative round-robin scheduler (M2.2).
 *
 * Tasks switch explicitly via k_task_yield() (asm, isr.S). Each task has its
 * own stack; yield saves the current registers + return address, asks the
 * scheduler for the next READY task, and resumes there (via `ret`, no iretq
 * frame gymnastics). The demo creates X/Y/Z tasks that print and yield.
 */
#include "kf_rt.h"

extern void isr_timer(void);
extern uint64_t k_task_yield(void);

#define MAX_TASKS   16
#define STACK_SIZE  16384
#define STACK_ARENA (256 * 1024)

typedef struct {
    uint64_t ctx;          /* saved context ptr (k_task_yield frame) */
    void (*entry)(void);
    uint8_t* stack;
    uint64_t id;
    int      state;        /* 0 READY, 1 FINISHED */
} kf_task_t;

static kf_task_t tasks[MAX_TASKS];
static uint64_t  task_count = 1;   /* index 0 = main */
static uint64_t  current = 0;
static uint64_t  next_id = 1;
static uint8_t   stack_arena[STACK_ARENA];
static uint64_t  arena_used = 0;

/* --- UART --- */
static void s_putc(char c) { __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0x3F8)); }

/* --- scheduler (cooperative) --- */
uint64_t k_sched_yield(uint64_t current_ctx) {
    tasks[current].ctx = current_ctx;
    for (uint64_t i = 1; i <= task_count; i++) {
        uint64_t next = (current + i) % task_count;
        if (tasks[next].state == 0) {
            current = next;
            return tasks[next].ctx;
        }
    }
    return current_ctx;
}

/* Build the initial k_task_yield frame so the first resume `ret`s into entry.
   Frame (from ctx): rbx,r12,r13,r14,r15,rbp,ret_addr. */
uint64_t k_task_create(void (*entry)(void)) {
    if (task_count >= MAX_TASKS || arena_used + STACK_SIZE > STACK_ARENA) return 0;
    uint8_t* stack = &stack_arena[arena_used];
    arena_used += STACK_SIZE;
    uint64_t top = (uint64_t)(uintptr_t)(stack + STACK_SIZE) & ~0xFULL;
    uint64_t* f = (uint64_t*)(top - 8 * 7);
    f[0] = 0;                        /* rbx */
    f[1] = 0;                        /* r12 */
    f[2] = 0;                        /* r13 */
    f[3] = 0;                        /* r14 */
    f[4] = 0;                        /* r15 */
    f[5] = 0;                        /* rbp */
    f[6] = (uint64_t)(uintptr_t)entry; /* return address -> task entry */
    uint64_t idx = task_count;
    tasks[idx].ctx   = (uint64_t)(uintptr_t)f;
    tasks[idx].entry = entry;
    tasks[idx].stack = stack;
    tasks[idx].id    = next_id++;
    tasks[idx].state = 0;
    task_count++;
    return tasks[idx].id;
}

/* --- demo tasks --- */
static void task_a(void) { for (;;) { s_putc('X'); k_task_yield(); } }
static void task_b(void) { for (;;) { s_putc('Y'); k_task_yield(); } }
static void task_c(void) { for (;;) { s_putc('Z'); k_task_yield(); } }

int64_t k_sched_init(void) {
    k_task_create(task_a);
    k_task_create(task_b);
    k_task_create(task_c);
    s_putc('[');
    /* round-robin via yield, starting from main (index 0) */
    for (;;) { k_task_yield(); }
    return 1;
}

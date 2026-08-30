/*  KengaOS — Syscall handler (C часть).
    Реализация системных вызовов.
*/
#include "syscall.h"
#include "io.h"
#include "../lib/libc.h"
#include "../drivers/fb.h"
#include "../drivers/uart.h"
#include "../sched/thread.h"
#include "../sched/scheduler.h"

void syscall_init(void) {
    /* Настроить EFER.SCE (Syscall Enable) */
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));   /* EFER */
    u64 efer = ((u64)hi << 32) | lo;
    efer |= (1ULL);   /* SCE bit 0 */
    lo = efer & 0xFFFFFFFF;
    hi = efer >> 32;
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000080));

    /* STAR: селекторы для syscall/sysret.
       bits 47:32  — CS для syscall (kernel)
       bits 63:48  — база для sysret (user)
       Kernel CS = 0x08, kernel DS/SS = 0x10
       SYSCALL: CS = STAR[47:32] = 0x08, SS = STAR[47:32]+8 = 0x10
       SYSRET : CS = STAR[63:48]+16 = 0x28 (+RPL3 → 0x2B),
                SS = STAR[63:48]+8  = 0x20 (+RPL3 → 0x23)
       GDT: index 5 = user code (0x28), index 4 = user data (0x20). */
    u64 star = ((u64)0x08 << 32) | ((u64)0x18 << 48);
    lo = star & 0xFFFFFFFF;
    hi = star >> 32;
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000081));

    /* LSTAR: адрес обработчика syscall */
    extern void syscall_entry(void);
    u64 entry = (u64)syscall_entry;
    lo = entry & 0xFFFFFFFF;
    hi = entry >> 32;
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000082));

    /* FMASK: маска для очистки RFLAGS при syscall. Очищаем IF. */
    u64 fmask = 0x200;   /* IF */
    lo = fmask & 0xFFFFFFFF;
    hi = fmask >> 32;
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000084));
}

/* Реализация системных вызовов. */
static u64 sys_exit(u64 code) {
    /* Завершить текущий user-процесс: пометить поток как FINISHED. */
    sched_mark_current_finished();
    /* Не возвращаемся — планировщик переключит на следующий.
       FMASK сбросил IF при входе в syscall: без sti таймер не разбудит
       hlt и планировщик не вернёт управление (зависание ядра). */
    for (;;) {
        __asm__ volatile ("sti; hlt");
    }
    return 0;
}

static u64 sys_write(u64 fd, u64 buf, u64 count) {
    if (fd != 1) return -1;   /* только stdout */
    /* buf — user-space адрес, но мы в kernel context с тем же CR3,
       так что можно читать напрямую. */
    const char *s = (const char*)buf;
    /* Вывод в framebuffer + UART */
    for (u64 i = 0; i < count; i++) {
        fb_putc(s[i]);
        uart_putc(UART_COM1, s[i]);
    }
    return count;
}

static u64 sys_yield(void) {
    /* Простая уступка: пометить как READY, пусть планировщик выберет другого. */
    if (thread_current()) {
        thread_current()->state = THREAD_READY;
    }
    return 0;
}

static u64 sys_get_pid(void) {
    struct thread *t = thread_current();
    return t ? t->id : 0;
}

static u64 sys_read(u64 fd, u64 buf, u64 count) {
    /* stdin не реализован в user-mode пока */
    return 0;
}

u64 syscall_handler(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    (void)a4; (void)a5;
    switch (num) {
        case SYS_EXIT:    return sys_exit(a1);
        case SYS_WRITE:   return sys_write(a1, a2, a3);
        case SYS_YIELD:   return sys_yield();
        case SYS_GET_PID: return sys_get_pid();
        case SYS_READ:    return sys_read(a1, a2, a3);
        default:          return (u64)-1;
    }
}

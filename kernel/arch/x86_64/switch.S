/*  KengaOS — Context switch (GAS/AT&T синтаксис).

    context_switch(from, to):
      1. Сохранить callee-saved регистры from на его стек
      2. Сохранить from->rsp = текущий rsp
      3. Загрузить to->rsp
      4. Восстановить callee-saved регистры to
      5. ret — вернётся в контекст to

    Callee-saved в SysV AMD64: rbx, rbp, r12, r13, r14, r15
    (не rdi/rsi/rdx/rcx/r8-r11 — они caller-saved).

    thread_trampoline:
      Вызывается при первом переключении на новый поток.
      На стеке подготовлено (сверху вниз, см. thread.c):
        [thread_trampoline addr]  ← switch.S сделает "ret" сюда
        [rflags]
        [thread_exit addr]
        [entry]
        [arg]
*/
.section .text

.global context_switch
.type   context_switch, @function
context_switch:
    /* rdi = from (struct thread*), rsi = to (struct thread*) */
    /* Сохранить callee-saved на текущий стек */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* from->rsp = rsp  (offsetof(struct thread, rsp) = 0x30) */
    movq %rsp, 0x30(%rdi)

    /* rsp = to->rsp */
    movq 0x30(%rsi), %rsp

    /* Восстановить callee-saved */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    ret
.size context_switch, .-context_switch

/* ============================================================
   thread_trampoline
   ============================================================

   Первый "ret" в switch.S при новом потоке возвращается сюда.
   Но на самом деле, мы положили адрес thread_trampoline на стек
   как адрес возврата, поэтому код ниже исполнится.

   Стек выглядит так (сверху = то, что pop'нется первым):
     [thread_exit]   ← ret address (если entry вернётся)
     [entry]         ← функция для вызова
     [arg]           ← её аргумент
*/
.global thread_trampoline
.type   thread_trampoline, @function
thread_trampoline:
    /* Новый поток стартует в контексте context_switch (ISR, IF=0).
       Включаем прерывания — иначе первый же hlt (thread_exit) уснёт
       навсегда, и планировщик не переключится. */
    sti
    popq %rax            /* rax = thread_exit */
    popq %rbx            /* rbx = entry */
    popq %rdi            /* rdi = arg (первый аргумент для entry) */
    pushq %rax           /* вернуть thread_exit на стек как return addr */
    jmp  *%rbx           /* call entry(arg) */
.size thread_trampoline, .-thread_trampoline

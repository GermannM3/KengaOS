/*  KengaOS — syscall entry (asm часть).
    Сохраняет user-регистры, вызывает syscall_handler, восстанавливает.

    При SYSCALL процессор делает:
      RCX = user RIP (return addr для SYSRET)
      R11 = user RFLAGS
      RFLAGS.IF = 0 (прерывания выключены)
      RIP = LSTAR (этот обработчик)
      CS = STAR[47:32] | 0   (kernel CS = 0x08)
      SS = STAR[47:32] + 8   (kernel SS = 0x10)

    Проблема: RSP НЕ меняется автоматически — он остаётся user RSP.
    Нужно переключиться на kernel stack. Способ: swapgs (меняет GS base).

    В нашем ядре gs:0 указывает на переменную kernel_rsp_storage.
    Когда мы входим в syscall, swapgs переключает GS на kernel base,
    мы читаем kernel RSP оттуда, на выходе — обратно.

    Соглашение user-ABI (userlib.h): RAX = номер, RDI = a1, RSI = a2,
    RDX = a3, R10 = a4, R8 = a5.
    C-обработчик: syscall_handler(num, a1, a2, a3, a4, a5)
      → RDI = num, RSI = a1, RDX = a2, RCX = a3, R8 = a4, R9 = a5.
*/
.section .text
.global syscall_entry
.global kernel_rsp_storage
.global user_rsp_storage

/* Глобальные переменные для обмена stack pointers. */
.section .data
.balign 8
kernel_rsp_storage:
    .quad 0
user_rsp_storage:
    .quad 0

.section .text
.type   syscall_entry, @function
syscall_entry:
    /* 1. swapgs: переключить GS base на kernel (IA32_KERNEL_GS_BASE) */
    swapgs

    /* 2. Сохранить user RSP. */
    movq %rsp, user_rsp_storage(%rip)
    /* 3. Загрузить kernel RSP. */
    movq kernel_rsp_storage(%rip), %rsp

    /* 4. Сохранить user-контекст возврата и аргументы. */
    pushq %rcx        /* user RIP (для sysret) */
    pushq %r11        /* user RFLAGS (для sysret) */
    pushq %r8         /* a5 */
    pushq %r10        /* a4 */
    pushq %rdx        /* a3 */
    pushq %rsi        /* a2 */
    pushq %rdi        /* a1 */

    /* 5. Сохранить callee-saved регистры (для handler). */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* 6. Вызвать syscall_handler(num, a1..a5).
          RAX = номер, остальные аргументы берём со стека.
          Раскладка: rsp+0 rbp, +8 rbx, +16 r12, +24 r13, +32 r14,
          +40 r15, +48 rdi(a1), +56 rsi(a2), +64 rdx(a3), +72 r10(a4),
          +80 r8(a5), +88 r11, +96 rcx. */
    movq %rax, %rdi                    /* num */
    movq 48(%rsp), %rsi                /* a1 (user rdi) */
    movq 56(%rsp), %rdx                /* a2 (user rsi) */
    movq 64(%rsp), %rcx                /* a3 (user rdx) */
    movq 72(%rsp), %r8                 /* a4 (user r10) */
    movq 80(%rsp), %r9                 /* a5 (user r8) */
    call syscall_handler
    /* Возвращаемое значение в RAX. */

    /* 7. Восстановить callee-saved. */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    /* 8. Восстановить user-аргументы и контекст возврата. */
    popq %rdi
    popq %rsi
    popq %rdx
    popq %r10
    popq %r8
    popq %r11
    popq %rcx

    /* 9. Переключиться обратно на user RSP. */
    movq user_rsp_storage(%rip), %rsp

    /* 10. swapgs: вернуть GS base обратно на user. */
    swapgs

    /* 11. sysretq: RIP←RCX, RFLAGS←R11, CS←STAR[63:48]+16, SS←STAR[63:48]+8 */
    sysretq
.size syscall_entry, .-syscall_entry

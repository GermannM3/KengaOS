/*  KengaOS — user_jump: переключение из kernel (ring 0) в user (ring 3).

    Использует iretq с RPL=3 в CS, что заставляет процессор:
      - Переключиться на ring 3
      - Загрузить SS:RSP из стека
      - Загрузить RFLAGS
      - Загрузить CS:RIP
      - Обновить сегментные регистры

    Соглашение (упрощённое):
      rdi = entry (user RIP)
      rsi = user_rsp
      rdx = pml4_phys (для CR3)

    Сразу после переключения CR3 мы находимся в kernel-stack,
    но с CR3 = user process. Положить на стек iretq frame:

      [ss]      = 0x23  (user data, ring 3)
      [rsp]     = user_rsp
      [rflags]  = 0x202 (IF=1)
      [cs]      = 0x1B  (user code, ring 3)
      [rip]     = entry

    Затем iretq — процессор переходит в user-mode.
*/
.section .text
.global user_jump
.type   user_jump, @function
user_jump:
    /* rdi = entry, rsi = user_rsp, rdx = pml4_phys */
    /* Переключить CR3 (это безопасно — мы сейчас в kernel mappings,
       которые shared между address spaces). */
    movq %rdx, %cr3

    /* Подготовить iretq frame на kernel stack.
       Сначала запретим прерывания на момент переключения. */
    cli

    /* Положить на стек (сверху вниз, по порядку для iretq):
       SS, RSP, RFLAGS, CS, RIP */
    pushq $0x23               /* SS = user data segment (0x20 | 3) */
    pushq %rsi                /* RSP = user_rsp */
    /* Фиксированный RFLAGS: IF=1 (и reserved bit 1).
       Нельзя pushfq после cli — утащили бы IF=0 в user-mode навсегда. */
    pushq $0x202              /* RFLAGS = IF=1 */
    /* CS = 0x2B: user code на индексе 5 (0x28|3), чтобы совпадать
       с тем, что возвращает SYSRET (STAR[63:48]+16 = 0x28|3). */
    pushq $0x2B               /* CS = user code segment (0x28 | 3) */
    pushq %rdi                /* RIP = entry */

    /* Обнулить регистры (необязательно, но для чистоты) */
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rbp, %rbp
    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15

    /* rdi, rsi — занулим тоже */
    xorq %rdi, %rdi
    xorq %rsi, %rsi

    /* Установить сегментные регистры user data */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* Перейти в user-mode */
    iretq
.size user_jump, .-user_jump

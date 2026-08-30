/*  KengaOS — ISR-обёртки для x86_64 (GAS/AT&T синтаксис).
    Каждая обёртка:
      1. Если исключение не pushes err_code — push фиктивный 0
      2. Push номер прерывания
      3. Сохраняет все GP-регистры
      4. Вызывает isr_handler(state)
      5. Восстанавливает регистры и делает iretq
*/
.section .text

.macro PUSH_REGS
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
.endm

.macro POP_REGS
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
.endm

.macro ISR_NOERR n
    .balign 8
    .global isr\n
    isr\n:
        pushq $0
        pushq $\n
        PUSH_REGS
        movq %rsp, %rdi
        call isr_handler
        POP_REGS
        addq $16, %rsp
        iretq
.endm

.macro ISR_ERR n
    .balign 8
    .global isr\n
    isr\n:
        pushq $\n
        PUSH_REGS
        movq %rsp, %rdi
        call isr_handler
        POP_REGS
        addq $16, %rsp
        iretq
.endm

/* 32 исключения */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR  10
ISR_ERR  11
ISR_ERR  12
ISR_ERR  13
ISR_ERR  14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR  17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR  30
ISR_NOERR 31

.macro IRQ n, vec
    .balign 8
    .global irq\n
    irq\n:
        pushq $0
        pushq $\vec
        PUSH_REGS
        movq %rsp, %rdi
        call isr_handler
        POP_REGS
        addq $16, %rsp
        iretq
.endm

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

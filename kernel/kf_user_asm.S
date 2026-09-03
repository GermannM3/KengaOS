/* kf_user_asm.S — x86_64: переход в ring 3 (iretq) и вход syscalls (int 0x80).
   Стаб совместим с форматом фрейма isr.S (kf_regs_t). */

.intel_syntax noprefix
.code64

.global user_jump
.global isr_syscall
.global k_user_exit_return

.extern k_syscall_handler
.extern k_user_is_done
.extern k_save_rsp
.extern k_save_ret

/* int64_t user_jump(u64 entry, u64 user_rsp, u64 pml4)
   rdi=entry, rsi=user_rsp, rdx=pml4.
   Сохраняем kernel-контекст, переключаем CR3, iretq в ring 3.
   После sys_exit пользовательского процесса стаб возвращает управление
   сюда же (восстановление контекста) — функция возвращает 0. */
user_jump:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov rax, offset k_user_exit_return
    mov [rip + k_save_ret], rax
    mov [rip + k_save_rsp], rsp

    /* CR3 = pml4 (rdx) */
    mov cr3, rdx

    /* сегменты данных ring 3: udata = слот 4 | RPL3 = 0x23.
       SS из CPL0 с RPL3 грузить нельзя (#GP) — SS корректно загрузит iretq. */
    mov ax, 0x23
    mov ds, ax
    mov es, ax


    /* iretq-фрейм: rip=entry, cs=0x1b (ucode64|RPL3), rflags=0x202, rsp, ss=0x2b */
    push 0x23
    push rsi
    push 0x202
    push 0x1b
    push rdi
    iretq

k_user_exit_return:
    xor eax, eax
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

/* int 0x80 стаб: фрейм как у isr_N (dummy-error, vector) + GPR. */
isr_syscall:
    push 0
    push 0x80
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    mov rdi, rsp
    call k_syscall_handler
    /* sys_exit: не возвращаемся в user — прыгаем в ядро */
    call k_user_is_done
    test eax, eax
    jnz .exit_path
    /* вернуть rax пользователю */
    mov rax, [rsp]        /* rax из фрейма (обновлён обработчиком) */
    add rsp, 17*8         /* 15 GPR + dummy + vector */
    iretq
.exit_path:
    add rsp, 17*8         /* снять фрейм */
    mov rsp, [rip + k_save_rsp]
    sti                   /* ядро вернулось из user: таймер нужен */
    jmp [rip + k_save_ret]

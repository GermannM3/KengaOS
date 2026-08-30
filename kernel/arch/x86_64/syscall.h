/*  KengaOS — Syscall interface (SYSCALL/SYSRET).
    AMD64 syscall mechanism — fast ring 3 → ring 0.

    Syscall numbers:
      0 = exit(code)             — завершить процесс
      1 = write(fd, buf, count)  — записать в fd (пока только stdout=1)
      2 = yield()                — уступить CPU
      3 = get_pid()              — получить PID текущего процесса
      4 = read(fd, buf, count)   — read из fd (stdin=0)
*/
#ifndef KENGA_SYSCALL_H
#define KENGA_SYSCALL_H

#include "../lib/types.h"

void syscall_init(void);

/* Syscall numbers */
#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_YIELD   2
#define SYS_GET_PID 3
#define SYS_READ    4

/* Обработчик syscall (вызывается из syscall_asm.S). */
u64 syscall_handler(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

#endif

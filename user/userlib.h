/*  KengaOS — User-space libc-заменитель.
    Загружается в каждый user-процесс. Делает syscall'ы.
*/
#ifndef KENGA_USERLIB_H
#define KENGA_USERLIB_H

typedef unsigned long size_t;
typedef long ssize_t;

/* Syscall wrappers. Делают inline asm SYSCALL. */
static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall1(long num, long a1) {
    return syscall3(num, a1, 0, 0);
}

static inline long syscall0(long num) {
    return syscall3(num, 0, 0, 0);
}

/* API */
#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_YIELD   2
#define SYS_GET_PID 3
#define SYS_READ    4

static inline void exit(int code) {
    syscall1(SYS_EXIT, code);
    __builtin_unreachable();
}

static inline ssize_t write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (long)buf, count);
}

static inline void yield(void) {
    syscall0(SYS_YIELD);
}

static inline long get_pid(void) {
    return syscall0(SYS_GET_PID);
}

/* Простая strlen. */
static inline size_t k_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* puts — write + newline. */
static inline int puts(const char *s) {
    size_t len = k_strlen(s);
    ssize_t w = write(1, s, len);
    write(1, "\n", 1);
    return (w == (ssize_t)len) ? 0 : -1;
}

#endif

/* KengaOS user-mode v1: hello через int 0x80 (sys_write), затем sys_exit. */
static void sys_write(const char* s, unsigned long len) {
    unsigned long ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(1), "D"(s), "S"(len));
}
static void sys_exit(void) {
    __asm__ __volatile__("int $0x80" : : "a"(2));
}
void _start(void) {
    const char* msg = "RING3: hello from user-mode!\n";
    unsigned long len = 29;
    sys_write(msg, len);
    sys_exit();
    for (;;) { }
}

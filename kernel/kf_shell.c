/* kf_shell.c — interactive shell (M2.3).
 *
 * Runs as a cooperative task: reads PS/2 keyboard chars, echoes them to the
 * framebuffer console, assembles a line and dispatches simple commands.
 * Idle (no input) -> k_task_yield() so other tasks get CPU time.
 */
#include "kf_rt.h"

extern uint64_t k_task_create(void (*entry)(void));
extern uint64_t k_task_yield(void);

#define SHELL_LINE 256
static char line[SHELL_LINE];
static int  li = 0;

static int scmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int sncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) { if (a[i] != b[i]) return 1; }
    return 0;
}

static const char* dec(int64_t n) {
    static char buf[24];
    int i = 0; unsigned long long v;
    if (n < 0) { buf[i++] = '-'; v = (unsigned long long)(-(n + 1)) + 1ull; }
    else v = (unsigned long long)n;
    char t[24]; int k = 0;
    do { t[k++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (k) buf[i++] = t[--k];
    buf[i] = 0;
    return buf;
}

static void run_cmd(const char* cmd) {
    if (cmd[0] == 0) { k_fb_con_print("kenga> "); return; }
    if (scmp(cmd, "help") == 0) {
        k_fb_con_print("commands:\n");
        k_fb_con_print("  help   - this list\n");
        k_fb_con_print("  clear  - clear screen\n");
        k_fb_con_print("  info   - kernel info\n");
        k_fb_con_print("  echo x - print x\n");
        k_fb_con_print("  mem    - memory info\n");
        k_fb_con_print("  tasks  - scheduler status\n");
    } else if (scmp(cmd, "clear") == 0) {
        k_fb_con_clear();
    } else if (scmp(cmd, "info") == 0) {
        k_fb_con_print("KengaOS v0.1 x86_64\n");
        k_fb_con_print("Kenga kernel over Limine, framebuffer console\n");
    } else if (scmp(cmd, "tasks") == 0) {
        k_fb_con_print("cooperative round-robin scheduler active\n");
    } else if (scmp(cmd, "mem") == 0) {
        int64_t freek = k_mem_free_bytes() / 1024;
        int64_t totk = k_mem_total_bytes() / 1024;
        k_fb_con_print("free heap: ");
        k_fb_con_print(dec(freek));
        k_fb_con_print(" KiB / ");
        k_fb_con_print(dec(totk));
        k_fb_con_print(" KiB usable\n");
    } else if (sncmp(cmd, "echo ", 5) == 0) {
        k_fb_con_print(cmd + 5);
        k_fb_con_print("\n");
    } else {
        k_fb_con_print("unknown command: ");
        k_fb_con_print(cmd);
        k_fb_con_print("\n");
    }
    k_fb_con_print("kenga> ");
}

static void shell_task(void) {
    k_fb_con_print("KengaOS shell v0.1\n");
    k_fb_con_print("type 'help'\n");
    k_fb_con_print("kenga> ");
    for (;;) {
        if (k_kbd_pending() == 0) { k_task_yield(); continue; }
        int c = (int)k_kbd_read();
        if (c == '\n') {
            line[li] = 0;
            k_fb_con_print("\n");
            run_cmd(line);
            li = 0;
        } else if (c == 8) {
            if (li > 0) { li--; k_fb_con_putc(8); }
        } else if (c >= 32 && li < SHELL_LINE - 1) {
            line[li++] = (char)c;
            char ch[2] = { (char)c, 0 };
            k_fb_con_print(ch);
        }
        k_task_yield();
    }
}

int64_t k_shell_init(void) {
    k_kbd_init();           /* PIC remap + IRQ1 -> vector 33 */
    k_fb_con_init();        /* clear console */
    k_task_create(shell_task);
    __asm__ __volatile__("sti");
    for (;;) k_task_yield();   /* main becomes the idle task */
    return 1;
}

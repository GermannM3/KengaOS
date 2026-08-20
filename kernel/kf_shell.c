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

static void run_cmd(const char* cmd) {
    if (cmd[0] == 0) { k_fb_con_print("kenga> "); return; }
    if (scmp(cmd, "help") == 0) {
        k_fb_con_print("commands:\n");
        k_fb_con_print("  help   - this list\n");
        k_fb_con_print("  clear  - clear screen\n");
        k_fb_con_print("  info   - kernel info\n");
        k_fb_con_print("  echo x - print x\n");
        k_fb_con_print("  mem    - memory info\n");
        k_fb_con_print("  ps     - list processes\n");
        k_fb_con_print("  log x  - IPC send 'x' to logger\n");
        k_fb_con_print("  ask x  - IPC round-trip to agent\n");
        k_fb_con_print("  ls     - list vfs files\n");
        k_fb_con_print("  cat x  - print vfs file\n");
        k_fb_con_print("  cpuinfo- CPU vendor/brand\n");
        k_fb_con_print("  date   - RTC date/time\n");
        k_fb_con_print("  time   - uptime\n");
        k_fb_con_print("  mmap   - memory map\n");
        k_fb_con_print("  tasks  - scheduler status\n");
    } else if (scmp(cmd, "clear") == 0) {
        k_fb_con_clear();
    } else if (scmp(cmd, "info") == 0) {
        k_fb_con_print("KengaOS v0.1 x86_64\n");
        k_fb_con_print("Kenga kernel over Limine, framebuffer console\n");
        k_fb_con_redraw();
    } else if (scmp(cmd, "tasks") == 0) {
        k_fb_con_print("cooperative round-robin scheduler active\n");
    } else if (scmp(cmd, "ps") == 0) {
        int64_t n = k_proc_count();
        k_fb_con_print("processes:\n");
        for (int64_t i = 0; i < n; i++) {
            k_fb_con_print("  pid ");
            k_fb_con_print(dec(k_proc_pid_at(i)));
            k_fb_con_print("  ");
            k_fb_con_print(k_proc_name_at(i));
            k_fb_con_print("\n");
        }
    } else if (sncmp(cmd, "log ", 4) == 0) {
        int64_t r = k_ipc_send(k_logger_pid(), cmd + 4);
        if (!r) k_fb_con_print("ipc queue full\n");
    } else if (sncmp(cmd, "ask ", 4) == 0) {
        /* IPC round-trip: send to agent, wait for reply, print it. */
        int64_t r = k_ipc_send(k_agent_pid(), cmd + 4);
        if (!r) { k_fb_con_print("ipc queue full\n"); }
        else {
            char reply[64];
            if (k_ipc_recv_str(reply, sizeof reply)) {
                k_fb_con_print(reply);
                k_fb_con_print("\n");
            }
        }
    } else if (scmp(cmd, "cpuinfo") == 0) {
        char buf[128];
        k_hw_cpu_vendor(buf, sizeof buf);
        k_fb_con_print("vendor: ");
        k_fb_con_print(buf);
        k_fb_con_print("\n");
        k_hw_cpu_brand(buf, sizeof buf);
        k_fb_con_print("brand : ");
        k_fb_con_print(buf);
        k_fb_con_print("\n");
    } else if (scmp(cmd, "date") == 0) {
        char buf[32];
        k_hw_rtc_str(buf, sizeof buf);
        k_fb_con_print(buf);
        k_fb_con_print("\n");
    } else if (scmp(cmd, "mmap") == 0) {
        int64_t n = k_mem_region_count();
        k_fb_con_print("mem regions: ");
        k_fb_con_print(dec(n));
        k_fb_con_print("\n");
        for (int64_t i = 0; i < n; i++) {
            k_fb_con_print("  ");
            k_fb_con_print(dec(k_mem_region_type(i)));
            k_fb_con_print(" @");
            k_fb_con_print(dec(k_mem_region_base(i)));
            k_fb_con_print(" +");
            k_fb_con_print(dec(k_mem_region_len(i)));
            k_fb_con_print("\n");
        }
    } else if (scmp(cmd, "time") == 0) {
        k_fb_con_print("uptime: ");
        k_fb_con_print(dec(k_time_uptime_ms() / 1000));
        k_fb_con_print(".");
        k_fb_con_print(dec(k_time_uptime_ms() % 1000));
        k_fb_con_print(" s\n");
    } else if (scmp(cmd, "ls") == 0) {
        int64_t n = k_vfs_count();
        for (int64_t i = 0; i < n; i++) {
            k_fb_con_print(k_vfs_name(i));
            k_fb_con_print("\n");
        }
    } else if (sncmp(cmd, "cat ", 4) == 0) {
        char buf[128];
        if (k_vfs_cat(cmd + 4, buf, sizeof buf)) {
            k_fb_con_print(buf);
        } else {
            k_fb_con_print("no such file: ");
            k_fb_con_print(cmd + 4);
            k_fb_con_print("\n");
        }
    } else if (scmp(cmd, "mem") == 0) {
        int64_t freek = k_mem_free_bytes() / 1024;
        int64_t totk = k_mem_total_bytes() / 1024;
        k_fb_con_print("heap: ");
        k_fb_con_print(dec(freek));
        k_fb_con_print(" KiB free / ");
        k_fb_con_print(dec(totk));
        k_fb_con_print(" KiB\n");
        k_fb_con_print("free frames: ");
        k_fb_con_print(dec(k_mem_pages_free() * 4));
        k_fb_con_print(" KiB\n");
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

static void sh_uart(const char* s) { for (; *s; s++) __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)*s), "Nd"((uint16_t)0x3F8)); }

int64_t k_shell_init(void) {
    k_kbd_init();           /* PIC remap + IRQ1 -> vector 33 */
    k_fb_con_init();        /* clear console */
    {   /* DIAG: cpu + rtc to UART */
        char b[128];
        k_hw_cpu_vendor(b, sizeof b); sh_uart("CPUV:"); sh_uart(b); sh_uart("\n");
        k_hw_cpu_brand(b, sizeof b); sh_uart("CPUB:"); sh_uart(b); sh_uart("\n");
        k_hw_rtc_str(b, sizeof b); sh_uart("RTC:"); sh_uart(b); sh_uart("\n");
        sh_uart("NREG:"); sh_uart(dec(k_mem_region_count())); sh_uart("\n");
    }
    k_proc_init();          /* spawn logger + agent (IPC) */
    k_proc_spawn("shell", shell_task);   /* register shell so it can recv IPC */
    __asm__ __volatile__("sti");
    for (;;) k_task_yield();   /* main becomes the idle task */
    return 1;
}

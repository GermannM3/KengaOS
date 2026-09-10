/* kf_shell.c — interactive shell (M2.3).
 *
 * Runs as a cooperative task: reads PS/2 keyboard chars, echoes them to the
 * framebuffer console, assembles a line and dispatches simple commands.
 * Idle (no input) -> k_task_yield() so other tasks get CPU time.
 */
#include "kf_rt.h"

static void sh_uart(const char* s) { for (; *s; s++) __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)*s), "Nd"((uint16_t)0x3F8)); }

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


/* ---------- pipes ("bash" v1): a | b | c ------------------------------
   Стадии — те же команды шелла, но фильтры читают stdin-буфер и пишут
   в stdout-буфер. Последняя стадия печатает в консоль. Полноценный
   userspace-bash ждёт fork/exec — это честный первый крест. */

#define PIPE_MAX  4
#define PIPE_BUF  512

static char* sskip(const char* s) { while (*s == ' ') s++; return (char*)s; }
static void strim(char* s) {
    int n = 0; while (s[n]) n++;
    while (n && (s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
}

static void filt_upper(const char* in, char* out) {
    int i = 0;
    for (; in[i] && i < PIPE_BUF - 1; i++)
        out[i] = (in[i] >= 'a' && in[i] <= 'z') ? in[i] - 32 : in[i];
    out[i] = 0;
}

static void filt_count(const char* in, char* out) {
    /* wc: lines words chars */
    int lines = 0, words = 0, chars = 0, inword = 0;
    for (const char* s = in; *s; s++) {
        chars++;
        if (*s == '\n') lines++;
        if (*s == ' ' || *s == '\n' || *s == '\t') inword = 0;
        else if (!inword) { inword = 1; words++; }
    }
    if (chars && !lines) lines = 1;            /* последняя строка без \n */
    /* dec() возвращает статический буфер — собрать вручную */
    char tmp[4][16]; int lens[4];
    int vals[3] = { lines, words, chars };
    for (int k = 0; k < 3; k++) {
        int v = vals[k], t = 0;
        if (!v) tmp[k][t++] = '0';
        while (v) { tmp[k][t++] = (char)('0' + v % 10); v /= 10; }
        lens[k] = t;
    }
    int o = 0;
    for (int k = 0; k < 3; k++) {
        while (lens[k]) out[o++] = tmp[k][--lens[k]];
        out[o++] = (k == 2) ? 0 : ' ';
    }
}

/* диспетчер стадии: фильтры получают in->out; unknown -> out="?" */
static void run_stage(const char* cmd, const char* in, char* out) {
    out[0] = 0;
    if (cmd[0] == 0) { return; }
    if (sncmp(cmd, "echo ", 5) == 0) {
        int i = 0;
        const char* s = cmd + 5;
        for (; s[i] && i < PIPE_BUF - 2; i++) out[i] = s[i];
        out[i++] = '\n'; out[i] = 0;
    } else if (scmp(cmd, "upper") == 0) {
        filt_upper(in, out);
    } else if (scmp(cmd, "count") == 0) {
        filt_count(in, out);
    } else if (scmp(cmd, "cat") == 0) {
        /* cat без аргумента = пропустить stdin (как cat) */
        int i = 0;
        for (; in[i] && i < PIPE_BUF - 1; i++) out[i] = in[i];
        out[i] = 0;
    } else if (sncmp(cmd, "cat ", 4) == 0) {
        char buf[128];
        if (k_vfs_cat(cmd + 4, buf, sizeof buf)) {
            int i = 0;
            for (; buf[i] && i < PIPE_BUF - 1; i++) out[i] = buf[i];
            out[i] = 0;
        }
    } else {
        out[0] = '?'; out[1] = 0;
    }
}

static int exec_pipe(char* line) {
    /* есть ли '|' в строке? */
    int has = 0;
    for (const char* s = line; *s; s++) if (*s == '|') { has = 1; break; }
    if (!has) return 0;

    char stages[PIPE_MAX][64];
    char bufs[2][PIPE_BUF];
    int n = 0, si = 0;
    const char* s = line;
    while (*s && n < PIPE_MAX) {
        if (*s == '|') { stages[n][si] = 0; strim(stages[n]); n++; si = 0; s++; continue; }
        if (si < 63) stages[n][si++] = *s;
        s++;
    }
    stages[n][si] = 0; strim(stages[n]); n++;

    char* in = bufs[0]; in[0] = 0;
    for (int i = 0; i < n; i++) {
        char* out = bufs[(i + 1) & 1];
        run_stage(stages[i], in, out);
        in = out;
    }
    k_fb_con_print(in);
    if (in[0] == '?' ) k_fb_con_print("  (only echo/upper/count/cat pipe here; full bash = userspace)\n");
    return 1;
}

static void run_cmd(const char* cmd) {
    if (cmd[0] == 0) { k_fb_con_print("kenga> "); return; }
    if (exec_pipe((char*)cmd)) { k_fb_con_print("kenga> "); return; }
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
        k_fb_con_print("  spawn x- agent creates an agent\n");
        k_fb_con_print("  demo   - living-OS showcase\n");
        k_fb_con_print("  agents - list agents\n");
        k_fb_con_print("  caps id [v] - show/set capabilities\n");
        k_fb_con_print("  model a b - neural agent predicts XOR\n");
        k_fb_con_print("  ls     - list vfs files\n");
        k_fb_con_print("  cat x  - print vfs file\n");
        k_fb_con_print("  ver    - git version\n");
        k_fb_con_print("  cpuinfo- CPU vendor/brand\n");
        k_fb_con_print("  date   - RTC date/time\n");
        k_fb_con_print("  time   - uptime\n");
        k_fb_con_print("  mmap   - memory map\n");
        k_fb_con_print("  reboot - restart\n");
        k_fb_con_print("  poweroff - shutdown\n");
        k_fb_con_print("  tasks  - scheduler status\n");
    } else if (scmp(cmd, "clear") == 0) {
        k_fb_con_clear();
    } else if (scmp(cmd, "info") == 0) {
        char b[128];
        k_fb_con_print("KengaOS x86_64\n");
        k_hw_cpu_brand(b, sizeof b);
        k_fb_con_print("cpu: ");
        k_fb_con_print(b);
        k_fb_con_print("\n");
        k_fb_con_print("heap free: ");
        k_fb_con_print(dec(k_mem_free_bytes() / 1024));
        k_fb_con_print(" KiB, frames: ");
        k_fb_con_print(dec(k_mem_pages_free()));
        k_fb_con_print("\n");
        k_fb_con_print("uptime: ");
        k_fb_con_print(dec(k_time_uptime_ms() / 1000));
        k_fb_con_print(" s\n");
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
        /* IPC round-trip to an agent. "ask <text>" -> root agent;
           "ask <pid> <text>" -> that agent. */
        int64_t target = k_agent_pid();
        const char* txt = cmd + 4;
        int64_t n = 0, digits = 0;
        while (txt[digits] >= '0' && txt[digits] <= '9') { n = n * 10 + (txt[digits] - '0'); digits++; }
        if (digits > 0 && txt[digits] == ' ') { target = n; txt += digits + 1; }
        int64_t r = k_ipc_send(target, txt);
        if (!r) { k_fb_con_print("ipc queue full\n"); }
        else {
            char reply[64];
            if (k_ipc_recv_str(reply, sizeof reply)) {
                k_fb_con_print(reply);
                k_fb_con_print("\n");
            }
        }
    } else if (sncmp(cmd, "spawn ", 6) == 0 || scmp(cmd, "spawn") == 0) {
        /* ask root agent to spawn a child agent */
        const char* name = (cmd[5] == ' ') ? cmd + 6 : "";
        char full[80]; int i = 0;
        const char* s = "spawn ";
        for (; *s && i < 79; i++) full[i] = *s++;
        for (; *name && i < 79; i++) full[i] = *name++;
        full[i] = 0;
        int64_t r = k_ipc_send(k_agent_pid(), full);
        if (!r) { k_fb_con_print("ipc queue full\n"); }
        else {
            char reply[64];
            if (k_ipc_recv_str(reply, sizeof reply)) { k_fb_con_print(reply); k_fb_con_print("\n"); }
        }
    } else if (scmp(cmd, "demo") == 0) {
        char rb[64];
        k_fb_con_print("=== KengaOS: living OS demo ===\n");
        if (k_ipc_send(k_agent_pid(), "hi")) {
            if (k_ipc_recv_str(rb, sizeof rb)) { k_fb_con_print("agent : "); k_fb_con_print(rb); k_fb_con_print("\n"); }
        }
        if (k_ipc_send(k_agent_pid(), "spawn kid")) {
            if (k_ipc_recv_str(rb, sizeof rb)) { k_fb_con_print("spawn : "); k_fb_con_print(rb); k_fb_con_print("\n"); }
        }
        if (k_ipc_send(k_agent_pid(), "remember who=kid")) {
            if (k_ipc_recv_str(rb, sizeof rb)) { k_fb_con_print("mem   : "); k_fb_con_print(rb); k_fb_con_print("\n"); }
        }
        if (k_ipc_send(k_agent_pid(), "recall who")) {
            if (k_ipc_recv_str(rb, sizeof rb)) { k_fb_con_print("recall: "); k_fb_con_print(rb); k_fb_con_print("\n"); }
        }
        if (k_vfs_cat("version", rb, sizeof rb)) { k_fb_con_print("ver   : "); k_fb_con_print(rb); }
        k_fb_con_print("time  : ");
        k_fb_con_print(dec(k_time_uptime_ms() / 1000));
        k_fb_con_print(" s uptime, ");
        k_fb_con_print(dec(k_mem_pages_free()));
        k_fb_con_print(" free frames\n");
        k_fb_con_print("=== end demo ===\n");
    } else if (sncmp(cmd, "model ", 6) == 0) {
        /* model <a> <b> -> neural agent infers XOR, replies predict <n> */
        int64_t r = k_ipc_send(k_model_pid(), cmd + 6);
        if (!r) { k_fb_con_print("ipc queue full\n"); }
        else {
            char reply[64];
            if (k_ipc_recv_str(reply, sizeof reply)) { k_fb_con_print(reply); k_fb_con_print("\n"); }
        }
    } else if (sncmp(cmd, "caps ", 5) == 0) {
        /* caps <pid> [value]: show or set an agent's capabilities (privileged). */
        int64_t pid = 0, val = -1; const char* s = cmd + 5;
        while (*s >= '0' && *s <= '9') { pid = pid * 10 + (*s - '0'); s++; }
        if (*s == ' ') {
            s++; int64_t v = 0;
            while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
            val = v;
        }
        if (val >= 0) {
            if (k_proc_set_caps(pid, val)) { k_fb_con_print("caps set\n"); }
            else k_fb_con_print("no such pid\n");
        } else {
            k_fb_con_print("caps: ");
            k_fb_con_print(dec(k_proc_caps(pid)));
            k_fb_con_print("\n");
        }
    } else if (scmp(cmd, "agents") == 0) {
        int64_t n = k_proc_count();
        sh_uart("AGENTS:");
        k_fb_con_print("agents/processes: ");
        k_fb_con_print(dec(n));
        k_fb_con_print("\n");
        for (int64_t i = 0; i < n; i++) {
            sh_uart(" ");
            sh_uart(dec(k_proc_pid_at(i)));
            sh_uart("=");
            sh_uart(k_proc_name_at(i));
            sh_uart(" p=");
            sh_uart(dec(k_proc_parent_at(i)));
            sh_uart(" c=");
            sh_uart(dec(k_proc_caps(k_proc_pid_at(i))));
            k_fb_con_print("  pid ");
            k_fb_con_print(dec(k_proc_pid_at(i)));
            k_fb_con_print("  ");
            k_fb_con_print(k_proc_name_at(i));
            k_fb_con_print("  parent=");
            k_fb_con_print(dec(k_proc_parent_at(i)));
            k_fb_con_print("  caps=");
            k_fb_con_print(dec(k_proc_caps(k_proc_pid_at(i))));
            k_fb_con_print("\n");
        }
        sh_uart("\n");
    } else if (scmp(cmd, "ver") == 0) {
        char b[128];
        if (k_vfs_cat("version", b, sizeof b)) { k_fb_con_print(b); }
        else { k_fb_con_print("KengaOS-dev\n"); }
    } else if (scmp(cmd, "reboot") == 0) {
        k_fb_con_print("rebooting...\n");
        k_power_reboot();
    } else if (scmp(cmd, "poweroff") == 0) {
        k_fb_con_print("shutting down...\n");
        k_power_shutdown();
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
    k_fb_con_print("Привет! Это KengaOS — ОС нового поколения\n");
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
    {   /* DIAG: cpu + rtc to UART */
        char b[128];
        k_hw_cpu_vendor(b, sizeof b); sh_uart("CPUV:"); sh_uart(b); sh_uart("\n");
        k_hw_cpu_brand(b, sizeof b); sh_uart("CPUB:"); sh_uart(b); sh_uart("\n");
        k_hw_rtc_str(b, sizeof b); sh_uart("RTC:"); sh_uart(b); sh_uart("\n");
        sh_uart("NREG:"); sh_uart(dec(k_mem_region_count())); sh_uart("\n");
    }
    /* PIPE self-test: echo kenga pipe test | upper | count -> "1 3 15" */
    {
        char stages[3][64];
        char a[PIPE_BUF], b[PIPE_BUF];
        const char* e = "echo kenga pipe test";
        int i = 0; for (; e[i]; i++) stages[0][i] = e[i];
        stages[0][i] = 0;
        stages[1][0]='u';stages[1][1]='p';stages[1][2]='p';stages[1][3]='e';stages[1][4]='r';stages[1][5]=0;
        stages[2][0]='c';stages[2][1]='o';stages[2][2]='u';stages[2][3]='n';stages[2][4]='t';stages[2][5]=0;
        a[0] = 0;
        run_stage(stages[0], "", b);
        run_stage(stages[1], b, a);
        run_stage(stages[2], a, b);
        sh_uart(b[0]=='1' && b[1]==' ' && b[2]=='3' && b[3]==' ' && b[4]=='1' && b[5]=='6'
                ? "PIPE OK" : "PIPE FAIL");
        sh_uart("\n");
    }
    k_proc_init();          /* spawn logger + agent (IPC) */
    k_proc_spawn("shell", shell_task, CAP_ALL);   /* system agent: full caps */
    __asm__ __volatile__("sti");
    for (;;) k_task_yield();   /* main becomes the idle task */
    return 1;
}

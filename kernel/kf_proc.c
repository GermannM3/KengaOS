/* kf_proc.c — lightweight processes + IPC (M2.5).
 *
 * A "process" is a scheduled task with a pid, a name and a per-process message
 * queue. Processes send/receive small string messages via k_ipc_send /
 * k_ipc_recv (recv blocks by yielding until a message arrives). Runs on top of
 * the cooperative scheduler (no address-space isolation yet — that needs
 * paging). A demo "logger" process prints whatever it receives.
 */
#include "kf_rt.h"

extern uint64_t k_task_create(void (*entry)(void));
extern uint64_t k_task_yield(void);
extern uint64_t k_sched_current(void);

#define MAX_PROC 16
#define IPC_QLEN 16
#define MSG_MAX  48

typedef struct {
    int64_t from;
    char    data[MSG_MAX];
} ipc_msg;

#define MEM_SLOTS 8

typedef struct {
    char key[16];
    char val[32];
} mem_slot;

typedef struct {
    int64_t      pid;
    const char*  name;
    uint64_t     task;
    int          active;
    uint64_t     caps;           /* capability bitmask               */
    ipc_msg      q[IPC_QLEN];
    int          qh, qt;
    mem_slot     mem[MEM_SLOTS];   /* per-agent living memory */
} kf_proc_t;

static kf_proc_t procs[MAX_PROC];
static int64_t   next_pid = 1;
static int64_t   logger_pid = 0;
static int64_t   agent_pid = 0;

static void lg_uart(const char* s) { for (; *s; s++) __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)*s), "Nd"((uint16_t)0x3F8)); }

int64_t k_proc_spawn(const char* name, void (*entry)(void), uint64_t caps) {
    for (int i = 0; i < MAX_PROC; i++) if (!procs[i].active) {
        procs[i].pid = next_pid++;
        procs[i].name = name;
        procs[i].task = k_task_create(entry);
        procs[i].active = 1;
        procs[i].caps = caps;
        procs[i].qh = procs[i].qt = 0;
        procs[i].mem[0].key[0] = 0;
        return procs[i].pid;
    }
    return 0;
}

/* Capability helpers. */
int64_t k_proc_caps(int64_t pid) {
    for (int i = 0; i < MAX_PROC; i++)
        if (procs[i].active && procs[i].pid == pid) return (int64_t)procs[i].caps;
    return 0;
}
int64_t k_proc_set_caps(int64_t pid, int64_t caps) {
    for (int i = 0; i < MAX_PROC; i++)
        if (procs[i].active && procs[i].pid == pid) { procs[i].caps = (uint64_t)caps; return 1; }
    return 0;
}

/* Send a message to the process with the given pid. */
int64_t k_ipc_send(int64_t pid, const char* data) {
    for (int i = 0; i < MAX_PROC; i++) if (procs[i].active && procs[i].pid == pid) {
        int next = (procs[i].qh + 1) % IPC_QLEN;
        if (next == procs[i].qt) return 0;              /* queue full */
        int k = 0;
        procs[i].q[procs[i].qh].from = k_sched_current();
        for (; data && data[k] && k < MSG_MAX - 1; k++) procs[i].q[procs[i].qh].data[k] = data[k];
        procs[i].q[procs[i].qh].data[k] = 0;
        procs[i].qh = next;
        return 1;
    }
    return 0;
}

/* Blocking receive: yield until a message arrives for the calling process.
   Returns 1 with the message copied into *out (data + from). */
int64_t k_ipc_recv(ipc_msg* out) {
    uint64_t me = k_sched_current();
    for (;;) {
        for (int i = 0; i < MAX_PROC; i++) {
            if (procs[i].active && procs[i].task == me && procs[i].qh != procs[i].qt) {
                *out = procs[i].q[procs[i].qt];
                procs[i].qt = (procs[i].qt + 1) % IPC_QLEN;
                return 1;
            }
        }
        k_task_yield();
    }
}

/* Receive a message into a string buffer (blocking). */
int64_t k_ipc_recv_str(char* buf, int max) {
    ipc_msg m;
    if (!k_ipc_recv(&m)) return 0;
    int k = 0;
    for (; m.data[k] && k < max - 1; k++) buf[k] = m.data[k];
    buf[k] = 0;
    return 1;
}

int64_t k_proc_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROC; i++) if (procs[i].active) n++;
    return n;
}

int64_t k_proc_pid_at(int64_t idx) { return procs[idx].pid; }
const char* k_proc_name_at(int64_t idx) { return procs[idx].active ? procs[idx].name : ""; }

/* --- logger process: prints received messages to console + UART --- */
static void logger_proc(void) {
    for (;;) {
        ipc_msg m;
        k_ipc_recv(&m);
        k_fb_con_print("logger[");
        k_fb_con_print(dec(m.from));
        k_fb_con_print("]: ");
        k_fb_con_print(m.data);
        k_fb_con_print("\n");
        lg_uart("LOGGER: "); lg_uart(m.data); lg_uart("\n");
        k_task_yield();
    }
}

/* --- kenga-agent: an agent can CREATE agents.
   On "spawn <name>" it spawns a child agent process (which can spawn its own),
   on anything else it echoes "ack: <text>". This is the "agent creates agents"
   foundation. */
/* find this process's slot (by current task index) */
static kf_proc_t* cur_proc(void) {
    uint64_t me = k_sched_current();
    for (int i = 0; i < MAX_PROC; i++)
        if (procs[i].active && procs[i].task == me) return &procs[i];
    return 0;
}

static void agent_remember(kf_proc_t* p, const char* key, const char* val) {
    for (int i = 0; i < MEM_SLOTS; i++) {
        int same = 1;
        for (int j = 0; key[j] && p->mem[i].key[j] && j < 15; j++) if (key[j] != p->mem[i].key[j]) { same = 0; break; }
        if (same && p->mem[i].key[0]) { /* overwrite existing */
            int x = 0; for (; val[x] && x < 31; x++) p->mem[i].val[x] = val[x]; p->mem[i].val[x] = 0;
            return;
        }
    }
    for (int i = 0; i < MEM_SLOTS; i++) {
        if (!p->mem[i].key[0]) {
            int x = 0; for (; key[x] && x < 15; x++) p->mem[i].key[x] = key[x]; p->mem[i].key[x] = 0;
            x = 0; for (; val[x] && x < 31; x++) p->mem[i].val[x] = val[x]; p->mem[i].val[x] = 0;
            return;
        }
    }
}

static const char* agent_recall(kf_proc_t* p, const char* key) {
    for (int i = 0; i < MEM_SLOTS; i++) {
        int same = 1, j;
        for (j = 0; key[j] && p->mem[i].key[j] && j < 15; j++) if (key[j] != p->mem[i].key[j]) { same = 0; break; }
        if (same && key[j] == p->mem[i].key[j] && p->mem[i].key[0]) return p->mem[i].val;
    }
    return "?";
}

static void agent_proc(void) {
    for (;;) {
        ipc_msg m;
        k_ipc_recv(&m);
        lg_uart("AGENT: "); lg_uart(m.data); lg_uart("\n");
        char reply[MSG_MAX];
        int k = 0;
        const char* pre;
        const char* d;
        kf_proc_t* me = cur_proc();
        if (m.data[0] == 's' && m.data[1] == 'p' && m.data[2] == 'a' && m.data[3] == 'w' && m.data[4] == 'n') {
            /* spawn a child agent; optional name after "spawn ", and
               optional "caps=<hex>" to grant capabilities. Requires CAP_SPAWN. */
            if (!me || !(me->caps & CAP_SPAWN)) {
                pre = "denied: no spawn capability";
                for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
            } else {
                const char* name = m.data + 5;
                while (*name == ' ') name++;
                uint64_t child_caps = CAP_AGENT;
                if (name[0]=='c' && name[1]=='a' && name[2]=='p' && name[3]=='s' && name[4]=='=') {
                    /* caps=0xN only (name defaults to 'agent') */
                    const char* hx = name + 5; uint64_t v = 0;
                    while ((*hx>='0'&&*hx<='9')||(*hx>='a'&&*hx<='f')) { v <<= 4; v |= (*hx<='9')?(*hx-'0'):(*hx-'a'+10); hx++; }
                    child_caps = v;
                    name = "agent";
                }
                if (!*name) name = "agent";
                int64_t child = k_proc_spawn(name, agent_proc, child_caps);
                pre = "spawned pid ";
                for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
                d = dec(child);
                for (; *d && k < MSG_MAX - 1; k++) reply[k] = *d++;
            }
        } else if (m.data[0]=='r' && m.data[1]=='e' && m.data[2]=='m' && me) {
            /* remember <key>=<val> */
            const char* s = m.data + 7;
            while (*s == ' ') s++;
            char key[16]; int ki = 0;
            while (*s && *s != '=' && ki < 15) key[ki++] = *s++;
            key[ki] = 0;
            if (*s == '=') s++;
            while (*s == ' ') s++;
            agent_remember(me, key, s);
            pre = "ok: remembered ";
            for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
            for (int ki2 = 0; key[ki2] && k < MSG_MAX - 1; ki2++) reply[k++] = key[ki2];
        } else if (m.data[0]=='r' && m.data[1]=='e' && m.data[2]=='c' && m.data[3]=='a' && m.data[4]=='l' && me) {
            /* recall <key> */
            const char* s = m.data + 6;
            while (*s == ' ') s++;
            const char* v = agent_recall(me, s);
            pre = "recall: ";
            for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
            for (; *v && k < MSG_MAX - 1; k++) reply[k] = *v++;
        } else {
            /* Russian-friendly agent: greet back, else ack.
               "п"/"П" in UTF-8 are 0xD0 0xBF / 0xD0 0x9F. */
            int is_hi =
                ((unsigned char)m.data[0]==0xD0 && (unsigned char)m.data[1]==0xBF) || /* п */
                ((unsigned char)m.data[0]==0xD0 && (unsigned char)m.data[1]==0x9F) || /* П */
                (m.data[0]=='p' && m.data[1]=='r' && m.data[2]=='i') ||
                (m.data[0]=='h' && m.data[1]=='i');
            if (is_hi) {
                d = "привет! я живой агент KengaOS";
                for (; *d && k < MSG_MAX - 1; k++) reply[k] = *d++;
            } else {
                pre = "ack: ";
                for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
                d = m.data;
                for (; *d && k < MSG_MAX - 1; k++) reply[k] = *d++;
            }
        }
        reply[k] = 0;
        k_ipc_send(m.from, reply);
        k_task_yield();
    }
}

/* "init": spawns the session processes (logger + kenga-agent). */
int64_t k_proc_init(void) {
    logger_pid = k_proc_spawn("logger", logger_proc, CAP_IPC);
    agent_pid  = k_proc_spawn("agent", agent_proc, CAP_ALL);   /* system agent */
    return logger_pid;
}

int64_t k_logger_pid(void) { return logger_pid; }
int64_t k_agent_pid(void) { return agent_pid; }

/* helper for printing numbers to the console (used above) */
const char* dec(int64_t n) {
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

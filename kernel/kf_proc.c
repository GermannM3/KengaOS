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

/* UI windows (agent-created, CAP_UI). */
#define MAX_WINDOWS 8
#define WIN_TITLE 24
#define WIN_TEXT  48

/* forward decls (window API is defined later in this file) */
int64_t k_ui_register_window(const char* title, const char* text);
int64_t k_ui_window_set_text(int64_t idx, const char* text);

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
    int64_t      parent;         /* who spawned this process          */
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
    int64_t parent = 0;
    uint64_t me = k_sched_current();
    for (int j = 0; j < MAX_PROC; j++)
        if (procs[j].active && procs[j].task == me) { parent = procs[j].pid; break; }
    for (int i = 0; i < MAX_PROC; i++) if (!procs[i].active) {
        procs[i].pid = next_pid++;
        procs[i].parent = parent;
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

/* Non-blocking poll: 1 if a message is queued for the calling process. */
int64_t k_ipc_poll(void) {
    uint64_t me = k_sched_current();
    for (int i = 0; i < MAX_PROC; i++)
        if (procs[i].active && procs[i].task == me && procs[i].qh != procs[i].qt) return 1;
    return 0;
}

/* Live IPC backlog length for a pid (0 if unknown). Used by the desktop
   Agents view to show per-agent message queue occupancy. */
int64_t k_proc_qlen(int64_t pid) {
    for (int i = 0; i < MAX_PROC; i++)
        if (procs[i].active && procs[i].pid == pid) {
            int n = (procs[i].qh - procs[i].qt + IPC_QLEN) % IPC_QLEN;
            return (int64_t)n;
        }
    return 0;
}

int64_t k_proc_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROC; i++) if (procs[i].active) n++;
    return n;
}

int64_t k_proc_pid_at(int64_t idx) { return procs[idx].pid; }
int64_t k_proc_parent_at(int64_t idx) { return procs[idx].parent; }
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
    static int64_t chat_win = 0;
    for (;;) {
        ipc_msg m;
        k_ipc_recv(&m);
        lg_uart("AGENT: "); lg_uart(m.data); lg_uart("\n");
        k_ui_log("agent: "); k_ui_log(m.data);
        char reply[MSG_MAX];
        int k = 0;
        const char* pre;
        const char* d;
        kf_proc_t* me = cur_proc();
        if (m.data[0] == 'c' && m.data[1] == 'h' && m.data[2] == 'a' && m.data[3] == 't') {
            /* desktop chat line -> agent. Reply and show the answer in the
               agent's own window (agent-native UI). */
            const char* q = m.data + 4;
            while (*q == ' ') q++;
            if (chat_win == 0) chat_win = k_ui_register_window("Agent", "chat");
            /* compose the answer */
            char ans[WIN_TEXT]; int ak = 0;
            pre = "ack: ";
            for (; *pre && ak < WIN_TEXT-1; ak++) ans[ak] = *pre++;
            for (; *q && ak < WIN_TEXT-1; ak++) ans[ak] = *q++;
            ans[ak] = 0;
            if (chat_win > 0) k_ui_window_set_text(chat_win - 1, ans);
            k = 0; pre = "chat ok";
            for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
        } else if (m.data[0] == 's' && m.data[1] == 'p' && m.data[2] == 'a' && m.data[3] == 'w' && m.data[4] == 'n') {
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
        } else if (m.data[0]=='u' && m.data[1]=='i' && me) {
            /* ui <title>: agent with CAP_UI creates a window on the desktop */
            if (!(me->caps & CAP_UI)) {
                pre = "denied: no ui capability";
                for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
            } else {
                const char* s = m.data + 2;
                while (*s == ' ') s++;
                char title[24]; int ti = 0;
                while (*s && *s != '|' && ti < 23) title[ti++] = *s++;
                title[ti] = 0;
                const char* text = "";
                if (*s == '|') { s++; while (*s == ' ') s++; text = s; }
                int64_t wid = k_ui_register_window(title, text);
                if (wid > 0) {
                    pre = "window id ";
                    for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
                    d = dec(wid);
                    for (; *d && k < MSG_MAX - 1; k++) reply[k] = *d++;
                } else {
                    pre = "denied: window table full or no ui";
                    for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
                }
            }
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

/* --- model-agent: runs the in-kernel MLP via IPC (CAP_MODEL_INFER). --- */
static int64_t model_pid = 0;
static void model_proc(void) {
    for (;;) {
        ipc_msg m;
        k_ipc_recv(&m);
        lg_uart("MODEL: "); lg_uart(m.data); lg_uart("\n");
        k_ui_log("model: "); k_ui_log(m.data);
        /* only respond to requests that start with a digit ("a b").
           Replies like "ack: ..." would otherwise echo forever. */
        const char* p0 = m.data;
        while (*p0 == ' ') p0++;
        if (*p0 < '0' || *p0 > '9') { k_task_yield(); continue; }
        /* parse "a b" -> infer XOR -> reply "predict <n>" */
        int64_t a = 0, b = 0; const char* s = m.data;
        while (*s == ' ') s++;
        while (*s >= '0' && *s <= '9') { a = a * 10 + (*s - '0'); s++; }
        while (*s == ' ') s++;
        while (*s >= '0' && *s <= '9') { b = b * 10 + (*s - '0'); s++; }
        int64_t r = k_model_infer(a, b);
        char reply[MSG_MAX]; int k = 0;
        const char* pre = "predict ";
        for (; *pre && k < MSG_MAX - 1; k++) reply[k] = *pre++;
        const char* dv = dec(r);
        for (; *dv && k < MSG_MAX - 1; k++) reply[k] = *dv++;
        reply[k] = 0;
        k_ipc_send(m.from, reply);
        k_task_yield();
    }
}

/* --- researcher agent: an agent with CAP_UI that opens its own window. --- */
static int64_t researcher_pid = 0;
static void researcher_proc(void) {
    /* IPC at startup: ask the model agent and the system agent before we
       settle into serving IPC ourselves. This exercises agent-to-agent IPC
       the moment the OS comes up. We read the replies here so they don't
       loop back into our own service loop. */
    lg_uart("RESEARCHER: starting\n");
    /* -> model: "1 0" (XOR of 1 and 0 == 1) — model_proc expects "a b" */
    k_ipc_send(model_pid, "1 0");
    k_task_yield();
    ipc_msg mr; int got_model = k_ipc_recv(&mr);
    if (got_model) { lg_uart("RESEARCHER: model said "); lg_uart(mr.data); lg_uart("\n"); }
    /* -> agent: "hi" (Russian greeting reply) */
    k_ipc_send(agent_pid, "hi");
    k_task_yield();
    ipc_msg ar; int got_agent = k_ipc_recv(&ar);
    if (got_agent) { lg_uart("RESEARCHER: agent said "); lg_uart(ar.data); lg_uart("\n"); }
    /* open a window on the desktop as soon as we run (CAP_UI required) */
    int64_t wid = k_ui_register_window("Research", "researcher online | model ready");
    lg_uart("RESEARCHER: window "); lg_uart(dec(wid)); lg_uart("\n");
    /* live demo: update the window text through the same API an IPC "upd"
       message uses — proves agent -> its own window works at startup. */
    k_ui_window_set_text(wid - 1, "online | IPC + model ready");
    lg_uart("RESEARCHER: window text updated\n");
    int64_t reqs = 0;
    int64_t last_upd = 0;
    int64_t last_ping = -1;
    for (;;) {
        if (k_ipc_poll()) {
            ipc_msg m;
            k_ipc_recv(&m);
            reqs++;
            lg_uart("RESEARCHER: "); lg_uart(m.data); lg_uart("\n");
            k_ui_log("researcher: "); k_ui_log(m.data);
            char reply[MSG_MAX]; int k = 0;
            kf_proc_t* me = cur_proc();
            /* "predict ..." — reply to our own ping: swallow, no ack (prevents
               an infinite ping-pong with the model agent). */
            if (m.data[0]=='p' && m.data[1]=='r' && m.data[2]=='e' && m.data[3]=='d') {
                k_task_yield();
                continue;
            }
            /* "upd <text>" -> update this agent's window text over IPC */
            if (m.data[0]=='u' && m.data[1]=='p' && m.data[2]=='d' && me && wid > 0) {
                const char* s = m.data + 3;
                while (*s == ' ') s++;
                k_ui_window_set_text(wid - 1, s);
                const char* pre = "window updated";
                for (; *pre && k < MSG_MAX-1; k++) reply[k] = *pre++;
            } else
            /* "infer a b" -> run the MLP and report */
            if (m.data[0]=='i' && m.data[1]=='n' && m.data[2]=='f' && m.data[3]=='e' && m.data[4]=='r' && me) {
                if (!(me->caps & CAP_MODEL_INFER)) {
                    const char* pre = "denied: no model capability";
                    for (; *pre && k < MSG_MAX-1; k++) reply[k] = *pre++;
                } else {
                    int64_t a=0, b=0; const char* s = m.data + 5;
                    while (*s==' ') s++;
                    while (*s>='0'&&*s<='9'){ a=a*10+(*s-'0'); s++; }
                    while (*s==' ') s++;
                    while (*s>='0'&&*s<='9'){ b=b*10+(*s-'0'); s++; }
                    int64_t r = k_model_infer(a, b);
                    const char* pre = "xor ";
                    for (; *pre && k < MSG_MAX-1; k++) reply[k] = *pre++;
                    const char* dv = dec(r);
                    for (; *dv && k < MSG_MAX-1; k++) reply[k] = *dv++;
                }
            } else if (m.data[0]=='w' && m.data[1]=='i' && m.data[2]=='n' && me) {
                /* "win <title>|<text>" -> open another window (CAP_UI) */
                if (!(me->caps & CAP_UI)) {
                    const char* pre = "denied: no ui capability";
                    for (; *pre && k < MSG_MAX-1; k++) reply[k] = *pre++;
                } else {
                    const char* s = m.data + 3;
                    while (*s==' ') s++;
                    char title[24]; int ti=0;
                    while (*s && *s!='|' && ti<23) title[ti++]=*s++;
                    title[ti]=0;
                    const char* text = "";
                    if (*s=='|'){ s++; while(*s==' ') s++; text = s; }
                    int64_t w = k_ui_register_window(title, text);
                    const char* pre = "window ";
                    for (; *pre && k < MSG_MAX-1; k++) reply[k] = *pre++;
                    const char* dv = dec(w);
                    for (; *dv && k < MSG_MAX-1; k++) reply[k] = *dv++;
                }
            } else {
                const char* pre = "ack: ";
                for (; *pre && k < MSG_MAX-1; k++) reply[k] = *pre++;
                const char* d = m.data;
                for (; *d && k < MSG_MAX-1; k++) reply[k] = *d++;
            }
            reply[k] = 0;
            k_ipc_send(m.from, reply);
        } else {
            /* live: refresh our window every ~1s so the UI shows agent state
               without any IPC command (agent-native living window). */
            int64_t now = k_time_uptime_ms() / 1000;
            if (now != last_upd && wid > 0) {
                last_upd = now;
                char txt[WIN_TEXT];
                const char* pre = "online | up ";
                int k = 0;
                for (; *pre && k < WIN_TEXT-1; k++) txt[k] = *pre++;
                const char* dv = dec(now);
                for (; *dv && k < WIN_TEXT-1; k++) txt[k] = *dv++;
                const char* su = "s | reqs ";
                for (; *su && k < WIN_TEXT-1; k++) txt[k] = *su++;
                const char* dq = dec(reqs);
                for (; *dq && k < WIN_TEXT-1; k++) txt[k] = *dq++;
                txt[k] = 0;
                k_ui_window_set_text(wid - 1, txt);
            }
            /* every ~3s ping the model agent: keeps IPC traffic alive so the
               Agents view shows live queue occupancy, and exercises the net. */
            if (now % 3 == 0 && now != last_ping) {
                last_ping = now;
                k_ipc_send(model_pid, "1 0");
            }
        }
        k_task_yield();
    }
}

/* "init": spawns the session processes (logger + kenga-agent + model-agent). */
int64_t k_proc_init(void) {
    logger_pid = k_proc_spawn("logger", logger_proc, CAP_IPC);
    agent_pid  = k_proc_spawn("agent", agent_proc, CAP_ALL);   /* system agent */
    k_model_init();
    model_pid = k_proc_spawn("model", model_proc, CAP_IPC|CAP_MODEL_INFER|CAP_MODEL_LOAD);
    researcher_pid = k_proc_spawn("researcher", researcher_proc,
                                  CAP_IPC|CAP_UI|CAP_MODEL_INFER);
    {   /* DIAG: verify XOR predictions + raw weights */
        lg_uart("XOR:00="); lg_uart(dec(k_model_infer(0,0)));
        lg_uart(" 01="); lg_uart(dec(k_model_infer(0,1)));
        lg_uart(" 10="); lg_uart(dec(k_model_infer(1,0)));
        lg_uart(" 11="); lg_uart(dec(k_model_infer(1,1))); lg_uart("\n");
    }
    return logger_pid;
}

int64_t k_logger_pid(void) { return logger_pid; }
int64_t k_agent_pid(void) { return agent_pid; }
int64_t k_model_pid(void) { return model_pid; }
int64_t k_researcher_pid(void) { return researcher_pid; }

/* --- UI: agent-created windows (CAP_UI). -----------------------------
   The desktop (desktop.kenga) polls these via intrinsics and draws them.
   Registering requires the calling process to hold CAP_UI. */

typedef struct {
    int64_t    x, y, w, h;
    int64_t    z;                 /* topmost = highest z */
    char       title[WIN_TITLE];
    char       text[WIN_TEXT];
    int        active;
} kf_win_t;

static kf_win_t wins[MAX_WINDOWS];
static int64_t  next_z = 1;

static int64_t k_ui_open_window(const char* title, const char* text);

int64_t k_ui_register_window(const char* title, const char* text) {
    kf_proc_t* me = cur_proc();
    if (!me || !(me->caps & CAP_UI)) return -1;   /* capability check */
    return k_ui_open_window(title, text);
}

/* shared open helper: cascade position by number of open windows */
static int64_t k_ui_open_window(const char* title, const char* text) {
    int open = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active) open++;
    for (int i = 0; i < MAX_WINDOWS; i++) if (!wins[i].active) {
        int step = open % 4;
        wins[i].x = 480 + step * 70;
        wins[i].y = 120 + step * 60;
        wins[i].w = 280; wins[i].h = 140;
        if (wins[i].x + 280 > 930) wins[i].x = 930 - 280;    /* keep off the log */
        if (wins[i].y + 140 > 640) wins[i].y = 640 - 140;    /* keep off the input bar */
        wins[i].w = 320; wins[i].h = 200;
        wins[i].z = next_z++;
        wins[i].active = 1;
        int k = 0;
        for (; title && title[k] && k < WIN_TITLE-1; k++) wins[i].title[k] = title[k];
        wins[i].title[k] = 0;
        k = 0;
        for (; text && text[k] && k < WIN_TEXT-1; k++) wins[i].text[k] = text[k];
        wins[i].text[k] = 0;
        return (int64_t)(i + 1);
    }
    return 0;   /* full */
}

/* System window: the graphics server itself may open windows (no CAP_UI
   required — it IS the UI authority). Returns window index (0-based). */
int64_t k_ui_system_window(const char* title, const char* text) {
    return k_ui_open_window(title, text);
}

int64_t k_ui_window_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active) n++;
    return n;
}

int64_t k_ui_window_x(int64_t idx)  { return wins[idx].x; }
int64_t k_ui_window_y(int64_t idx)  { return wins[idx].y; }
int64_t k_ui_window_w(int64_t idx)  { return wins[idx].w; }
int64_t k_ui_window_h(int64_t idx)  { return wins[idx].h; }
int64_t k_ui_window_z(int64_t idx)  { return wins[idx].z; }
const char* k_ui_window_title(int64_t idx) { return wins[idx].active ? wins[idx].title : ""; }
const char* k_ui_window_text(int64_t idx)  { return wins[idx].active ? wins[idx].text : ""; }

/* bring window to front on click */
int64_t k_ui_window_front(int64_t idx) {
    if (idx >= 0 && idx < MAX_WINDOWS && wins[idx].active) { wins[idx].z = next_z++; return 1; }
    return 0;
}

/* move a window (drag). idx is 0-based. clamps to screen edges. */
int64_t k_ui_window_move(int64_t idx, int64_t x, int64_t y) {
    if (idx < 0 || idx >= MAX_WINDOWS || !wins[idx].active) return 0;
    int64_t W = k_fb_width();
    int64_t H = k_fb_height();
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + wins[idx].w > W) x = W - wins[idx].w;
    if (y + wins[idx].h > H) y = H - wins[idx].h;
    wins[idx].x = x;
    wins[idx].y = y;
    return 1;
}

/* update a window's text over IPC (agent -> its own window). idx 0-based. */
int64_t k_ui_window_set_text(int64_t idx, const char* text) {
    if (idx < 0 || idx >= MAX_WINDOWS || !wins[idx].active) return 0;
    int k = 0;
    for (; text && text[k] && k < WIN_TEXT-1; k++) wins[idx].text[k] = text[k];
    wins[idx].text[k] = 0;
    return 1;
}

/* close a window. idx 0-based. */
int64_t k_ui_window_close(int64_t idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !wins[idx].active) return 0;
    wins[idx].active = 0;
    wins[idx].title[0] = 0;
    wins[idx].text[0] = 0;
    return 1;
}

/* --- desktop text input (agent chat bar). -----------------------------
   The Kenga desktop accumulates typed characters here, draws them, and on
   Enter submits the line to the system agent as an IPC "chat" message.
   The agent replies and shows the answer in its own window (agent-native). */
static char  g_input[64];
static int   g_input_len = 0;

int64_t k_ui_input_putc(int64_t c) {
    if (c == '\b') {                    /* backspace */
        if (g_input_len > 0) g_input[--g_input_len] = 0;
        return 1;
    }
    if ((c >= 32 && c <= 126) && g_input_len < 63) {
        g_input[g_input_len++] = (char)c;
        g_input[g_input_len] = 0;
        return 1;
    }
    return 0;
}
int64_t k_ui_input_clear(void) { g_input_len = 0; g_input[0] = 0; return 1; }
const char* k_ui_input_str(void) { return g_input; }
int64_t k_ui_input_len(void) { return (int64_t)g_input_len; }

/* Send the typed line to the system agent as a "chat" message, clear input. */
int64_t k_ui_input_submit(void) {
    if (g_input_len == 0) return 0;
    char msg[80]; int k = 0;
    const char* pre = "chat ";
    for (; *pre && k < 79; k++) msg[k] = *pre++;
    for (int i = 0; i < g_input_len && k < 79; i++) msg[k++] = g_input[i];
    msg[k] = 0;
    g_input_len = 0; g_input[0] = 0;
    return k_ipc_send(agent_pid, msg);
}

/* --- live agent event log (notifications). ---------------------------
   A small ring buffer of the last N agent IPC events. Agents write here as
   they run; the Kenga desktop renders it (data in C, rendering in Kenga). */
#define AGENT_LOG_N 6
#define AGENT_LOG_LEN 40
static char g_agent_log[AGENT_LOG_N][AGENT_LOG_LEN];
static int  g_agent_log_idx = 0;

int64_t k_ui_log(const char* s) {
    int k = 0;
    for (; s && s[k] && k < AGENT_LOG_LEN-1; k++) g_agent_log[g_agent_log_idx][k] = s[k];
    g_agent_log[g_agent_log_idx][k] = 0;
    g_agent_log_idx = (g_agent_log_idx + 1) % AGENT_LOG_N;
    return 1;
}
const char* k_ui_log_at(int64_t i) {
    if (i < 0 || i >= AGENT_LOG_N) return "";
    return g_agent_log[(g_agent_log_idx + i) % AGENT_LOG_N];
}

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

/* kf_user.c — user-mode (ring 3) для единого ядра: страничные таблицы,
   ELF64-загрузчик, запуск через iretq, syscall int 0x80 (write/exit).
   Порт и развитие kenga-os v0.0.5 user/vmm.
   ponytail v1: один foreground-процесс (iretq с сохранением kernel-контекста,
   sys_exit возвращает в ядро); многозадачный ring3 — этап 2.
*/
#include "kf_rt.h"

/* --- FFI соседей --- */
extern int64_t k_mem_palloc(void);
extern int64_t k_vfs_count(void);
extern const char* k_vfs_name(int64_t idx);
extern int64_t k_vfs_cat(const char* name, char* out, int max);
extern int64_t k_intr_set_gate(int64_t n, int64_t off);
extern int64_t k_kf_get_hhdm(void);

#ifdef __aarch64__
void k_arch_uart_putc(char c);
static void u_putc(char c) { k_arch_uart_putc(c); }
#else
static void u_putc(char c) {
    /* Ждём готовности передатчика (LSR.5): QEMU ДРОПАЕТ байты, если писать в
       0x3F8 быстрее, чем chardev разбирает FIFO. Именно так из лога пропадали
       куски вывода приложения, тогда как редкие сообщения ядра доходили. */
    for (int guard = 0; guard < 100000; guard++) {
        uint8_t lsr;
        __asm__ __volatile__("inb %1,%0" : "=a"(lsr) : "Nd"((uint16_t)0x3FD));
        if (lsr & 0x20) break;
    }
    __asm__ __volatile__("outb %0,%1" : : "a"((uint8_t)c), "Nd"((uint16_t)0x3F8));
}
#endif
int64_t k_user_syscall_gate(int64_t handler);   /* определён ниже */

#define USER_BASE      0x400000ull
#define USER_STACK_TOP 0x7ffff000ull
#define USER_STACK_SZ  0x10000ull
#define PAGE_SIZE      0x1000ull
#define PTE_USER       (1ull << 2)
#define PTE_WRITABLE   (1ull << 1)
#define PTE_PRESENT    1ull
#define PTE_NX         (1ull << 63)

static uint64_t hhdm = 0;
static uint64_t user_pml4 = 0;      /* phys текущего user-PML4 */
static volatile uint32_t user_done = 0;
uint64_t k_save_rsp = 0, k_save_ret = 0;   /* asm (kf_user_asm.S) */
/* Кадр, на который вернётся isr_syscall. Обычный syscall ставит свой же кадр;
   планировщик может подставить кадр другого процесса (см. kf_user_asm.S). */
uint64_t k_resume_frame = 0;

/* Планировщик ring-3 процессов (определён ниже, после TSS) */
static int up_yield_current(void* frame_v);
static int up_exit_current(void);
static int64_t up_pid_current(void);
static void up_note_stack_use(void);
static int up_sleep_current(uint64_t ms);
static int up_exit_with_code(uint64_t code);
static int64_t up_child_status(uint64_t pid);
static int64_t fd_open(uint64_t path_uva);
static int64_t fd_read(uint64_t fd, uint64_t uva, uint64_t len);
static int64_t fd_close(uint64_t fd);

/* k_mem_palloc возвращает УЖЕ отображённый VA (phys+hhdm).
   Для PTE нужен физический: va - hhdm. */
extern int64_t k_mem_virt_to_phys(int64_t addr);
static uint64_t va2pa(uint64_t va) { return va - hhdm; }

static inline uint64_t rd_cr3(void) {
    uint64_t v; __asm__ __volatile__("mov %%cr3, %0" : "=r"(v)); return v & ~0xFFFull;
}
static inline void wr_cr3(uint64_t v) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(v & ~0xFFFull) : "memory");
}
static void* pv(uint64_t phys) { return (void*)(uintptr_t)(phys + hhdm); }

/* --- минимальный page-walk/mapper (4K-страницы, user PML4) --- */
static uint64_t pte_fetch(uint64_t pml4_phys, uint64_t vaddr, uint64_t flags, int create) {
    uint64_t* pml4 = pv(pml4_phys);
    int idxs[4] = { (int)((vaddr >> 39) & 0x1FF), (int)((vaddr >> 30) & 0x1FF),
                    (int)((vaddr >> 21) & 0x1FF), (int)((vaddr >> 12) & 0x1FF) };
    uint64_t* t = pml4;
    for (int lvl = 0; lvl < 3; lvl++) {          /* PML4 -> PDPT -> PD */
        uint64_t e = t[idxs[lvl]];
        if (!(e & PTE_PRESENT)) {
            if (!create) return 0;
            uint64_t np_va = (uint64_t)k_mem_palloc();
            if (!np_va) return 0;
            uint8_t* z = (uint8_t*)(uintptr_t)np_va;
            for (int b = 0; b < 4096; b++) z[b] = 0;
            e = va2pa(np_va) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
            t[idxs[lvl]] = e;
        }
        t = (uint64_t*)(uintptr_t)((e & 0x000ffffffffff000ull) + hhdm);
    }
    return (uint64_t)(uintptr_t)t;   /* PT-таблица (VA); PTE-индекс = idxs[3] */
}

static int map_user_page(uint64_t pml4_phys, uint64_t vaddr, uint64_t phys, uint64_t flags) {
    uint64_t pt = pte_fetch(pml4_phys, vaddr, flags, 1);
    if (!pt) return 0;
    uint64_t* pte = (uint64_t*)(uintptr_t)pt;
    int i1 = (vaddr >> 12) & 0x1FF;
    pte[i1] = phys | flags | PTE_PRESENT;
    return 1;
}

/* user VA -> phys (по таблицам процесса) */
static uint64_t user_v2p(uint64_t pml4_phys, uint64_t vaddr) {
    uint64_t* pml4 = pv(pml4_phys);
    int i4 = (vaddr >> 39) & 0x1FF, i3 = (vaddr >> 30) & 0x1FF,
        i2 = (vaddr >> 21) & 0x1FF;
    uint64_t e = pml4[i4];
    if (!(e & PTE_PRESENT)) return 0;
    uint64_t* pdpt = pv(e & 0x000ffffffffff000ull);
    e = pdpt[i3];
    if (!(e & PTE_PRESENT)) return 0;
    uint64_t* pd = pv(e & 0x000ffffffffff000ull);
    e = pd[i2];
    if (!(e & PTE_PRESENT)) return 0;
    if (e & (1ull << 7)) {   /* 2MB page */
        return (e & 0x000fffffffe00000ull) | (vaddr & 0x1fffffull);
    }
    uint64_t* pt = pv(e & 0x000ffffffffff000ull);
    e = pt[(vaddr >> 12) & 0x1FF];
    if (!(e & PTE_PRESENT)) return 0;
    return (e & 0x000ffffffffff000ull) | (vaddr & 0xFFFull);
}

/* скопировать верхнюю половину (kernel space) из текущего CR3 */
static uint64_t pml4_create(void) {
    uint64_t np_va = (uint64_t)k_mem_palloc();
    if (!np_va) return 0;
    uint64_t* src = pv(rd_cr3());
    uint64_t* dst = (uint64_t*)(uintptr_t)np_va;
    for (int i = 0; i < 4096 / 8; i++) dst[i] = 0;
    for (int i = 256; i < 512; i++) dst[i] = src[i];   /* higher half */
    return va2pa(np_va);
}

/* --- ELF64 --- */
struct elf64_ehdr {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));
struct elf64_phdr {
    uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align;
} __attribute__((packed));

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2

static void ulog_hx(uint64_t v) {
    const char* h = "0123456789abcdef";
    char out[19]; int n = 0;
    out[n++]='0'; out[n++]='x';
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int d = (int)((v >> i) & 0xF);
        if (d || started || i == 0) { out[n++] = h[d]; started = 1; }
    }
    out[n] = 0;
    for (int i = 0; i < n; i++) u_putc(out[i]);
}

/* --- syscall int 0x80: rax=1 write(rdi=buf,rsi=len); rax=2 exit --- */
static void ulog_putc(char c) { u_putc(c); }

static int elf_load(uint64_t pml4_phys, const uint8_t* data, uint64_t size, uint64_t* entry_out) {
    if (size < sizeof(struct elf64_ehdr)) return 0;
    struct elf64_ehdr eh;
    for (int i = 0; i < (int)sizeof(eh); i++) ((uint8_t*)&eh)[i] = data[i];
    if (eh.ident[0] != 0x7F || eh.ident[1] != 'E' || eh.ident[2] != 'L' || eh.ident[3] != 'F') return 0;
    if (eh.ident[4] != 2 || eh.ident[5] != 1) return 0;
    if (eh.type != 2 || eh.machine != 62) return 0;
    if (!eh.phoff || !eh.phnum) return 0;
    if (eh.phoff + (uint64_t)eh.phnum * sizeof(struct elf64_phdr) > size) return 0;

    for (int i = 0; i < eh.phnum; i++) {
        struct elf64_phdr ph;
        const uint8_t* phb = data + eh.phoff + (uint64_t)i * sizeof(ph);
        for (int b = 0; b < (int)sizeof(ph); b++) ((uint8_t*)&ph)[b] = phb[b];
        if (ph.type != PT_LOAD) continue;
        uint64_t flags = PTE_USER;
        if (ph.flags & PF_W) flags |= PTE_WRITABLE;
        uint64_t va = ph.vaddr & ~0xFFFull;
        uint64_t end = (ph.vaddr + ph.memsz + 0xFFFull) & ~0xFFFull;
        for (uint64_t page = va; page < end; page += PAGE_SIZE) {
            uint64_t pp_va = (uint64_t)k_mem_palloc();
            if (!pp_va) return 0;
            uint64_t pp = va2pa(pp_va);
            uint8_t* kv = (uint8_t*)(uintptr_t)pp_va;
            for (int b = 0; b < 4096; b++) kv[b] = 0;
            /* Смещение в файле для НАЧАЛА этой страницы. Первая страница
               сегмента начинается не с начала файлового диапазона, а с inoff
               внутри неё, поэтому для страниц ПОСЛЕ первой надо вычесть inoff:
               иначе данные копируются со сдвигом на inoff и код/данные, как
               только сегмент переходит через границу страницы, портятся.
               Именно это роняло приложение при росте ELF выше ~4.6 КиБ. */
            uint64_t inoff = page == va ? (ph.vaddr & 0xFFFull) : 0;
            uint64_t foff = ph.offset + (page - va) - (page == va ? 0 : inoff);
            uint64_t cp = PAGE_SIZE - inoff;
            /* Копируем ТОЛЬКО файловую часть сегмента (filesz); всё от filesz
               до memsz — это .bss и обязано остаться нулями. Без этой проверки
               в BSS уезжал «хвост» файла: у Kenga-приложения мусор попадал в
               переменную аллокатора, и первый же kf_alloc давал OOM. */
            uint64_t seg_file_end = ph.offset + ph.filesz;
            if (foff < size && foff < seg_file_end) {
                if (foff + cp > size) cp = size - foff;
                if (foff + cp > seg_file_end) cp = seg_file_end - foff;
                if (inoff + cp > PAGE_SIZE) cp = PAGE_SIZE - inoff;
                for (uint64_t b = 0; b < cp; b++) kv[inoff + b] = data[foff + b];
            }
            uint64_t pflags = flags;
            if (!(ph.flags & PF_X)) pflags |= PTE_NX;
            if (!map_user_page(pml4_phys, page, pp, pflags)) return 0;
        }
    }
    /* user-стек */
    for (uint64_t page = USER_STACK_TOP - USER_STACK_SZ; page < USER_STACK_TOP; page += PAGE_SIZE) {
        uint64_t pp_va = (uint64_t)k_mem_palloc();
        if (!pp_va) return 0;
        uint8_t* kv = (uint8_t*)(uintptr_t)pp_va;
        for (int b = 0; b < 4096; b++) kv[b] = 0;
        if (!map_user_page(pml4_phys, page, va2pa(pp_va), PTE_USER | PTE_WRITABLE)) return 0;
    }
    *entry_out = eh.entry;
    return 1;
}

/* --- ELF из VFS по имени --- */
int64_t k_user_exec_vfs(const char* name) {
    if (!hhdm) hhdm = (uint64_t)k_kf_get_hhdm();
    int64_t n = k_vfs_count();
    for (int64_t i = 0; i < n; i++) {
        const char* nm = k_vfs_name(i);
        int j = 0, match = 0;
        while (nm[j] && name[j]) { if (nm[j] != name[j]) break; j++; }
        if (!name[j] && nm[j] == 0) match = 1;
        if (!match) continue;
        static uint8_t buf[32768];
        for (int b = 0; b < 32768; b++) buf[b] = 0;
        k_vfs_cat(nm, (char*)buf, 32768);
        user_pml4 = pml4_create();
        if (!user_pml4) return -1;
        uint64_t entry = 0;
        if (!elf_load(user_pml4, buf, 32768, &entry)) { user_pml4 = 0; return -2; }
        return (int64_t)entry;
    }
    return 0;   /* не найден */
}

/* --- ELF из произвольного буфера (файл на KengaFS, ROM, что угодно) ---
   Тот же elf_load, что и для initrd: отделяем «откуда байты» от «как грузить».
   Возвращает entry point (>0) или <0; запуск — отдельно, через k_user_run(). */
int64_t k_user_exec_blob(int64_t addr, int64_t size) {
    if (!hhdm) hhdm = (uint64_t)k_kf_get_hhdm();
    if (!addr || size <= 0) return -1;
    user_pml4 = pml4_create();
    if (!user_pml4) return -1;
    uint64_t entry = 0;
    if (!elf_load(user_pml4, (const uint8_t*)(uintptr_t)addr, (uint64_t)size, &entry)) {
        user_pml4 = 0;
        return -2;
    }
    return (int64_t)entry;
}

/* запуск: не возвращается до sys_exit пользователя; возвращает 0 (exit). */
static uint64_t kernel_cr3 = 0;   /* CR3 ядра, сохранён до ухода в user */

int64_t k_user_run(int64_t entry) {
    if (!entry || !user_pml4) return -1;
    extern int user_jump(uint64_t entry, uint64_t rsp, uint64_t pml4);
    user_done = 0;
    kernel_cr3 = rd_cr3();          /* ЗАПОМНИТЬ kernel AS до ухода */
    int rc = user_jump((uint64_t)entry, USER_STACK_TOP - 16, user_pml4);
    if (kernel_cr3) wr_cr3(kernel_cr3);   /* вернуть kernel AS */
    return 0;
}


/* --- Мост к KengaFS для ring-3 приложений -------------------------------
   Файловая система целиком на Kenga, поэтому C-обработчик int 0x80 не лезет
   в неё сам: состояние ФС кладёт Kenga (k_user_set_fs), а файловые запросы
   уходят обратно в Kenga-функцию k_fs_syscall. Так логика ФС остаётся в
   одном месте (kernel/fs.kenga), а C — только перевозчик байтов между
   user-адресом и кадром обмена. */
static int64_t ux_ino = 0, ux_bmp = 0, ux_io = 0, ux_dat = 0, ux_rw = 0, ux_ok = 0;
static uint8_t* uxfer = 0;            /* кадр обмена (ядро <-> KengaFS) */
static char ux_path[128];

extern int64_t k_fs_syscall(int64_t ino, int64_t bmp, int64_t io, int64_t dat,
                            int64_t rw, int64_t ok, int64_t num,
                            int64_t a, int64_t b, int64_t c) __attribute__((weak));

int64_t k_user_set_fs(int64_t ino, int64_t bmp, int64_t io, int64_t dat,
                      int64_t rw, int64_t ok) {
    ux_ino = ino; ux_bmp = bmp; ux_io = io; ux_dat = dat; ux_rw = rw; ux_ok = ok;
    if (!uxfer) uxfer = (uint8_t*)(uintptr_t)k_mem_palloc();
    return uxfer ? 1 : 0;
}

/* скопировать NUL-terminated строку из user-памяти ядра в ux_path */
static int ux_copy_str(uint64_t uva, char* out, int max) {
    int i = 0;
    while (i < max - 1) {
        uint64_t pa = user_v2p(user_pml4, uva + (uint64_t)i);
        if (!pa) break;
        char c = *(char*)(uintptr_t)(pv(pa & ~0xFFFull) + (pa & 0xFFF));
        if (!c) break;
        out[i++] = c;
    }
    out[i] = 0;
    return i;
}

/* user -> ядро: побайтово через страницы user-адреса */
static uint64_t ux_from_user(uint64_t uva, uint8_t* dst, uint64_t n) {
    uint64_t off = 0;
    while (off < n) {
        uint64_t pa = user_v2p(user_pml4, uva + off);
        if (!pa) break;
        uint64_t inpage = pa & 0xFFF;
        uint64_t chunk = 0x1000 - inpage;
        if (chunk > n - off) chunk = n - off;
        const uint8_t* ksrc = (const uint8_t*)(uintptr_t)(pv(pa & ~0xFFFull)) + inpage;
        for (uint64_t b = 0; b < chunk; b++) dst[off + b] = ksrc[b];
        off += chunk;
    }
    return off;
}

/* ядро -> user: побайтово через страницы user-адреса */
static uint64_t ux_to_user(uint64_t uva, const uint8_t* src, uint64_t n) {
    uint64_t off = 0;
    while (off < n) {
        uint64_t pa = user_v2p(user_pml4, uva + off);
        if (!pa) break;
        uint64_t inpage = pa & 0xFFF;
        uint64_t chunk = 0x1000 - inpage;
        if (chunk > n - off) chunk = n - off;
        uint8_t* kdst = (uint8_t*)(uintptr_t)(pv(pa & ~0xFFFull)) + inpage;
        for (uint64_t b = 0; b < chunk; b++) kdst[b] = src[off + b];
        off += chunk;
    }
    return off;
}

void k_syscall_handler(void* frame_v) {
    uint64_t* f = (uint64_t*)frame_v;
    /* frame: rax,rbx,rcx,rdx,rsi,rdi,rbp,r8..r15,vector,error,rip,cs,rflags,rsp,ss */
    k_resume_frame = (uint64_t)(uintptr_t)frame_v;   /* по умолчанию — вернуться сюда */
    uint64_t num = f[0];
    up_note_stack_use();

    if (num == 1) {   /* write: rdi=buf(user va), rsi=len */
        uint64_t uva = f[5], len = f[4];
        if (len > 4096) len = 4096;
        uint64_t off = 0;
        while (off < len) {
            uint64_t va = uva + off;
            /* user_v2p возвращает PA УЖЕ с intra-page offset */
            uint64_t pa = user_v2p(user_pml4, va);
            if (!pa) break;
            uint64_t inpage = pa & 0xFFF;
            uint64_t chunk = 0x1000 - inpage;
            if (chunk > len - off) chunk = len - off;
            uint8_t* ksrc = pv(pa & ~0xFFFull) + inpage;
            for (uint64_t b = 0; b < chunk; b++) {
                char ch = (char)ksrc[b];
                u_putc(ch);
                off++;
                if (!ch) break;
            }
        }
        f[0] = (uint64_t)off;   /* возврат в rax */
    } else if (num == 2) {      /* exit */
        f[0] = 0;
        if (up_exit_current() != 1) user_done = 1;   /* некого будить -> в ядро */
    } else if (num == 3) {      /* yield */
        f[0] = 0;
        if (up_yield_current(frame_v) != 1) user_done = 1;
    } else if (num == 4) {      /* getpid */
        f[0] = (uint64_t)up_pid_current();
    } else if (num == 27) {     /* open(path) -> fd или -1 */
        f[0] = (uint64_t)fd_open(f[5]);
    } else if (num == 28) {     /* read(fd, buf, len) -> сколько прочитано */
        f[0] = (uint64_t)fd_read(f[5], f[4], f[3]);
    } else if (num == 29) {     /* close(fd) */
        f[0] = (uint64_t)fd_close(f[5]);
    } else if (num == 26) {     /* wait(pid): БЛОКИРУЮЩИЙ сбор статуса */
        uint64_t want = f[5];
        int64_t st = up_child_status(want);
        if (st >= 0) {
            f[0] = (uint64_t)st;
        } else if (up_sleep_current(5) == 1) {
            /* Ребёнок ещё жив: сдвигаем сохранённый RIP на саму инструкцию
               int 0x80 (2 байта), чтобы при пробуждении ядро заново вошло в
               этот обработчик. Так блокирующий вызов не требует, чтобы
               обработчик «продолжился» — он просто переисполняется. */
            f[17] -= 2;
            /* RAX должен снова содержать НОМЕР syscall: приложение исполнит
               int 0x80 заново, минуя mov $26, %eax — иначе ядро увидит -1. */
            f[0] = num;
        } else {
            f[0] = (uint64_t)-1;   /* некого будить — приложение повторит */
        }
    } else if (num == 25) {     /* wait_status(pid) -> код завершения или -1 */
        f[0] = (uint64_t)up_child_status(f[5]);
    } else if (num == 24) {     /* spawn(path) -> pid: запустить приложение */
        uint64_t path_uva = f[5];
        int64_t pid = -1;
        if (uxfer && &k_fs_syscall) {
            ux_copy_str(path_uva, ux_path, (int)sizeof ux_path);
            int64_t n = k_fs_syscall(ux_ino, ux_bmp, ux_io, ux_dat, ux_rw, ux_ok,
                                     16, (int64_t)(uintptr_t)ux_path,
                                     k_xbuf(), k_xbuf_size());
            if (n > 0) {
                /* exec_blob перезапишет global user_pml4 на PML4 ребёнка —
                   родителю он ещё нужен для его собственных syscall'ов. */
                uint64_t parent_pml4 = user_pml4;
                pid = k_user_spawn_blob(k_xbuf(), n);
                user_pml4 = parent_pml4;
            }
        }
        f[0] = (uint64_t)pid;
    } else if (num == 23) {     /* exit_code(code): завершиться с кодом */
        f[0] = 0;
        if (up_exit_with_code(f[5]) != 1) user_done = 1;
    } else if (num == 21) {     /* sleep(ms): поспать, отдав CPU другим */
        f[0] = 0;
        up_sleep_current(f[5]);
    } else if (num == 22) {     /* uptime_ms() */
        f[0] = (uint64_t)k_time_uptime_ms();
    } else if (num == 16) {     /* cat(path, dst, max): файл KengaFS -> user */
        uint64_t path_uva = f[5], dst_uva = f[4], maxlen = f[3];
        int64_t n = -1;
        if (maxlen > 4096) maxlen = 4096;
        if (uxfer && maxlen > 0 && &k_fs_syscall) {
            ux_copy_str(path_uva, ux_path, (int)sizeof ux_path);
            n = k_fs_syscall(ux_ino, ux_bmp, ux_io, ux_dat, ux_rw, ux_ok,
                             16, (int64_t)(uintptr_t)ux_path,
                             (int64_t)(uintptr_t)uxfer, (int64_t)maxlen);
            if (n > 0) {
                if ((uint64_t)n > maxlen) n = (int64_t)maxlen;
                ux_to_user(dst_uva, uxfer, (uint64_t)n);
            }
        }
        f[0] = (uint64_t)n;
    } else if (num == 17) {     /* save(path, data, len): буфер приложения -> KengaFS */
        uint64_t path_uva = f[5], data_uva = f[4], len = f[3];
        int64_t n = -1;
        if (len > 4096) len = 4096;
        if (uxfer && len > 0 && &k_fs_syscall) {
            ux_copy_str(path_uva, ux_path, (int)sizeof ux_path);
            if (ux_from_user(data_uva, uxfer, len) == len) {
                n = k_fs_syscall(ux_ino, ux_bmp, ux_io, ux_dat, ux_rw, ux_ok,
                                 17, (int64_t)(uintptr_t)ux_path,
                                 (int64_t)(uintptr_t)uxfer, (int64_t)len);
            }
        }
        f[0] = (uint64_t)n;
    } else if (num == 18 || num == 19) {   /* mkdir(path) / rm(path) */
        uint64_t path_uva = f[5];
        int64_t n = -1;
        if (uxfer && &k_fs_syscall) {
            ux_copy_str(path_uva, ux_path, (int)sizeof ux_path);
            n = k_fs_syscall(ux_ino, ux_bmp, ux_io, ux_dat, ux_rw, ux_ok,
                             (int64_t)num, (int64_t)(uintptr_t)ux_path, 0, 0);
        }
        f[0] = (uint64_t)n;
    } else {
        f[0] = (uint64_t)-1;
    }
}

uint32_t k_user_is_done(void) { return user_done; }
uint64_t k_user_pml4(void) { return user_pml4; }

/* --- GDT с ring-3 + TSS: у Limine v12.6 все сегменты DPL0, user mode
       требует свой GDT (порт gdt.c из kenga-os v0.0.5). --- */
static uint64_t user_gdt[8];        /* 0x00..0x37 (TSS занимает 2 слота) */
static uint8_t  user_tss[104];      /* 64-bit TSS */
static uint8_t  tss_rsp0_stack[16 * 1024];

#define UCODE64_SEL 0x1b          /* 0x18 | RPL3 */
#define UDATA_SEL   0x2b          /* 0x20 | RPL3 */
#define TSS_SEL     0x30          /* слоты 0x28 = kernel CS (IRQ-гейты!), TSS — выше */

static void gdt_set_entry(int n, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    uint8_t* e = (uint8_t*)&user_gdt[n];
    e[0] = limit & 0xFF; e[1] = (limit >> 8) & 0xFF;
    e[2] = base & 0xFF; e[3] = (base >> 8) & 0xFF; e[4] = (base >> 16) & 0xFF;
    e[5] = access;
    e[6] = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    e[7] = (base >> 24) & 0xFF;
}

static void k_user_gdt_install(void) {
    /* базовые сегменты ядра — копия лиминовских (kcode 0x9B/0xCF, kdata 0x93/0xCF) */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9B, 0xAF);   /* 0x08 kcode64: L=1 */
    gdt_set_entry(2, 0, 0xFFFFF, 0x93, 0xCF);   /* 0x10 kdata */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFB, 0xAF);   /* 0x18 ucode64: DPL3, L=1 */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF3, 0xCF);   /* 0x20 udata,  DPL3 */
    gdt_set_entry(5, 0, 0xFFFFF, 0x9B, 0xAF);   /* 0x28 kernel CS-копия: IDT-гейты
                                                   прерываний ссылаются на 0x28! */
    /* TSS: base = user_tss, limit = 103 (селектор 0x30, слоты 6-7) */
    uint64_t tbase = (uint64_t)(uintptr_t)user_tss;
    uint8_t* e = (uint8_t*)&user_gdt[6];
    e[0] = 103 & 0xFF; e[1] = (103 >> 8) & 0xFF;
    e[2] = tbase & 0xFF; e[3] = (tbase >> 8) & 0xFF; e[4] = (tbase >> 16) & 0xFF;
    e[5] = 0x89;                                 /* TSS available, DPL0 */
    e[6] = 0x00;
    e[7] = (tbase >> 24) & 0xFF;
    uint64_t* hi = &user_gdt[7];
    *hi = (tbase >> 32) & 0xFFFFFFFFull;

    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) gdtr = {
        (uint16_t)(sizeof(user_gdt) - 1),
        (uint64_t)(uintptr_t)user_gdt
    };
    __asm__ __volatile__("cli");   /* IF=0 на время подмены GDT: слот 0x28 (kernel CS)
                                      временно занят TSS — прерывание тут = фриз */
    __asm__ __volatile__("lgdt %0" : : "m"(gdtr));
    /* перезагрузка сегментов: far return на kcode (0x08) */
    __asm__ __volatile__(
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw $0, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        : : : "rax", "memory");
    /* TSS.RSP0 = отдельный kernel-стек для прерываний из ring 3 */
    uint64_t rsp0 = (uint64_t)(uintptr_t)(tss_rsp0_stack + sizeof(tss_rsp0_stack));
    uint32_t* t = (uint32_t*)(uintptr_t)user_tss;
    t[1] = (uint32_t)(rsp0 & 0xFFFFFFFF);       /* RSP0 low  (offset 4) */
    t[2] = (uint32_t)(rsp0 >> 32);              /* RSP0 high (offset 8) */
    __asm__ __volatile__("ltr %0" : : "r"((uint16_t)TSS_SEL));
    __asm__ __volatile__("sti");
}

/* --- Файловые дескрипторы приложения ------------------------------------
   Приложение читает файл КУСКАМИ: open читает файл целиком в свой слот (KengaFS
   отдаёт содержимое через тот же мост, что cat), read отдаёт очередную порцию
   в буфер приложения, close освобождает слот. Логика ФС по-прежнему на Kenga. */
#define FD_MAX 4
#define FD_BUF 8192

static uint8_t g_fd_buf[FD_MAX][FD_BUF];
static int32_t g_fd_size[FD_MAX];
static int32_t g_fd_off[FD_MAX];
static char    g_fd_path[FD_MAX][64];
static int     g_fd_used[FD_MAX];

static int64_t fd_open(uint64_t path_uva) {
    int s = -1;
    for (int i = 0; i < FD_MAX; i++) if (!g_fd_used[i]) { s = i; break; }
    if (s < 0) return -1;
    if (!uxfer || !&k_fs_syscall) return -1;
    ux_copy_str(path_uva, g_fd_path[s], 64);
    int64_t n = k_fs_syscall(ux_ino, ux_bmp, ux_io, ux_dat, ux_rw, ux_ok,
                             16, (int64_t)(uintptr_t)g_fd_path[s],
                             (int64_t)(uintptr_t)g_fd_buf[s], FD_BUF);
    if (n < 0) return -1;
    g_fd_size[s] = (int32_t)n;
    g_fd_off[s] = 0;
    g_fd_used[s] = 1;
    return s;
}

static int64_t fd_read(uint64_t fd, uint64_t uva, uint64_t len) {
    if (fd >= FD_MAX || !g_fd_used[fd]) return -1;
    int64_t avail = (int64_t)g_fd_size[fd] - (int64_t)g_fd_off[fd];
    if (avail <= 0) return 0;                       /* конец файла */
    int64_t n = (int64_t)len;
    if (n > avail) n = avail;
    uint64_t done = ux_to_user(uva, g_fd_buf[fd] + g_fd_off[fd], (uint64_t)n);
    g_fd_off[fd] += (int32_t)done;
    return (int64_t)done;
}

static int64_t fd_close(uint64_t fd) {
    if (fd < FD_MAX) g_fd_used[fd] = 0;
    return 0;
}

/* --- Кооперативная многозадачность ring-3 процессов ---------------------
   Каждый процесс: своя PML4 (elf_load), свой kernel-стек (TSS.RSP0 при входе
   из ring 3) и сохранённый кадр прерывания (15 GPR + vector/dummy + iretq-
   фрейм). yield/exit сохраняют кадр текущего и переключаются на кадр
   следующего: isr_syscall берёт rsp из k_resume_frame. Когда готовых нет —
   user_done=1, и asm возвращается в ядро по k_save_rsp/k_save_ret.
   Планировщик кооперативный: переключение только по syscall. */
#define UP_MAX   4
#define UP_STACK 65536        /* KengaFS-код в syscall-контексте уходит глубоко:
                                 на 16 КиБ переполнение уезжало в СОСЕДНИЙ стек
                                 и затирало сохранённый кадр соседнего процесса */
#define UP_FRAME 22            /* 15 GPR + vector + dummy + 5 iretq-слов */

typedef struct {
    int      state;            /* 0 свободен, 1 готов, 2 работает, 3 завершён */
    int64_t  pid;
    uint64_t pml4;
    uint64_t kstack_top;
    uint64_t frame;            /* сохранённый rsp кадра */
    uint64_t max_used;         /* максимум занятого kernel-стека (диагностика) */
    uint64_t wake;             /* до какого времени (мс) процесс спит */
    uint64_t code;             /* код завершения (syscall 23) */
} uproc_t;

#define UP_SLEEP 4             /* процесс спит до g_up[i].wake */

static uproc_t g_up[UP_MAX];
static int     g_up_count = 0;
static int     g_up_cur = -1;
static uint8_t g_up_stacks[UP_MAX][UP_STACK];

static void up_tss_rsp0(uint64_t rsp0) {
    uint32_t* t = (uint32_t*)(uintptr_t)user_tss;
    t[1] = (uint32_t)(rsp0 & 0xFFFFFFFFu);
    t[2] = (uint32_t)(rsp0 >> 32);
}

static int64_t up_register(uint64_t pml4, uint64_t entry) {
    if (g_up_count >= UP_MAX || !pml4 || !entry) return 0;
    int i = g_up_count++;
    uint64_t top = (uint64_t)(uintptr_t)(g_up_stacks[i] + UP_STACK) & ~0xFull;
    uint64_t* fr = (uint64_t*)(uintptr_t)(top - UP_FRAME * 8);
    for (int k = 0; k < UP_FRAME; k++) fr[k] = 0;
    fr[15] = 0x80;                 /* vector (как у isr_syscall) */
    fr[17] = entry;                /* rip */
    fr[18] = 0x1b;                 /* cs = ucode64|RPL3 */
    fr[19] = 0x202;                /* rflags (IF=1) */
    fr[20] = USER_STACK_TOP - 16;  /* rsp в user-стеке */
    fr[21] = 0x23;                 /* ss = udata|RPL3 */
    g_up[i].state = 1;
    g_up[i].pid = 100 + i;
    g_up[i].pml4 = pml4;
    g_up[i].kstack_top = top;
    g_up[i].frame = (uint64_t)(uintptr_t)fr;
    return g_up[i].pid;
}

static uint64_t up_activate(int i) {
    g_up_cur = i;
    g_up[i].state = 2;
    user_pml4 = g_up[i].pml4;
    wr_cr3(g_up[i].pml4);
    up_tss_rsp0(g_up[i].kstack_top);
    return g_up[i].frame;
}

static int up_pick_next(void) {
    if (g_up_count == 0) return -1;
    for (int k = 1; k <= g_up_count; k++) {
        int base = g_up_cur < 0 ? 0 : g_up_cur;
        int i = (base + k) % g_up_count;
        if (g_up[i].state == UP_SLEEP) {
            if ((uint64_t)k_time_uptime_ms() >= g_up[i].wake) g_up[i].state = 1;
            else continue;                       /* ещё спит */
        }
        if (g_up[i].state == 1) return i;
    }
    return -1;
}

static int up_any_sleeping(void) {
    for (int i = 0; i < g_up_count; i++) if (g_up[i].state == UP_SLEEP) return 1;
    return 0;
}

/* Ждать пробуждения, если все спят: idle-задачи нет, поэтому стоим на hlt,
   пока таймер не разбудит спящего. Без этого выход последнего активного
   процесса «забывал» спящих и ядро возвращалось раньше времени. */
static int up_pick_wait(void) {
    for (;;) {
        int nx = up_pick_next();
        if (nx >= 0) return nx;
        if (!up_any_sleeping()) return -1;
        __asm__ __volatile__("sti; hlt");
    }
}

/* сохранить кадр текущего, переключиться; 1 = есть следующий процесс */
static int up_switch_from(void* frame_v) {
    if (g_up_cur >= 0 && frame_v) g_up[g_up_cur].frame = (uint64_t)(uintptr_t)frame_v;
    int nx = up_pick_wait();
    if (nx < 0) return 0;
    k_resume_frame = up_activate(nx);
    return 1;
}

static int up_yield_current(void* frame_v) {
    if (g_up_count == 0) return 0;
    if (g_up_cur >= 0) g_up[g_up_cur].state = 1;
    return up_switch_from(frame_v);
}

/* Статус ребёнка: его код, если он уже завершился, иначе -1 (опрос).
   Записи завершённых процессов живут до конца k_user_sched_run. */
static int64_t up_child_status(uint64_t pid) {
    for (int i = 0; i < g_up_count; i++) {
        if ((uint64_t)g_up[i].pid == pid) {
            if (g_up[i].state == 3) return (int64_t)g_up[i].code;
            return -1;
        }
    }
    return -1;
}

/* Завершиться с кодом: код остаётся в таблице процессов (аналог wait-статуса). */
static int up_exit_with_code(uint64_t code) {
    if (g_up_cur >= 0 && g_up_cur < g_up_count && code < 256) g_up[g_up_cur].code = code;
    return up_exit_current();
}

/* Поспать ms миллисекунд: пометить себя спящим и уступить CPU. Если будить
   некого (idle-задачи нет) — не спим вовсе, чтобы не встать намертво. */
static int up_sleep_current(uint64_t ms) {
    if (g_up_count < 2 || g_up_cur < 0 || g_up_cur >= g_up_count) return 0;
    if (ms > 5000) ms = 5000;
    g_up[g_up_cur].wake = (uint64_t)k_time_uptime_ms() + ms;
    g_up[g_up_cur].state = UP_SLEEP;
    /* именно up_pick_wait: если все спят, стоим на hlt, пока таймер не
       разбудит. С up_pick_next процесс «просыпался» сразу же, и sleep
       получался короче запрошенного. */
    int nx = up_pick_wait();
    if (nx < 0) { g_up[g_up_cur].state = 2; return 0; }
    k_resume_frame = up_activate(nx);
    return 1;
}

static int up_exit_current(void) {
    if (g_up_count == 0) return 0;
    if (g_up_cur >= 0) g_up[g_up_cur].state = 3;
    return up_switch_from(0);
}

static int64_t up_pid_current(void) {
    return (g_up_cur >= 0) ? g_up[g_up_cur].pid : 0;
}

/* Насколько глубоко syscall-контекст залез в kernel-стек процесса. */
static void up_note_stack_use(void) {
    if (g_up_cur < 0 || g_up_cur >= g_up_count) return;
    uint64_t rsp_now;
    __asm__ __volatile__("mov %%rsp, %0" : "=r"(rsp_now));
    uint64_t used = g_up[g_up_cur].kstack_top - rsp_now;
    if (used > g_up[g_up_cur].max_used) g_up[g_up_cur].max_used = used;
}

/* Загрузить ELF по имени из initrd в НОВЫЙ процесс. Возвращает pid или 0. */
int64_t k_user_spawn_vfs(const char* name) {
    int64_t e = k_user_exec_vfs(name);
    if (e <= 0) return 0;
    return up_register(user_pml4, (uint64_t)e);
}

/* То же, но ELF лежит в буфере ядра (например, файл KengaFS). */
int64_t k_user_spawn_blob(int64_t addr, int64_t size) {
    int64_t e = k_user_exec_blob(addr, size);
    if (e <= 0) return 0;
    return up_register(user_pml4, (uint64_t)e);
}

/* Вытеснение по таймеру: кадр прерванного ring-3 процесса -> следующий.
   Тот же путь, что у sys_yield: isr_common вернётся по k_resume_frame. */
static uint64_t g_preempt = 0;

int64_t k_user_timer_preempt(int64_t frame) {
    if (g_up_count < 2 || g_up_cur < 0 || g_up_cur >= g_up_count) return 0;
    if (!frame) return 0;
    g_up[g_up_cur].state = 1;
    int r = up_switch_from((void*)(uintptr_t)frame);
    if (r == 1) g_preempt++;
    return r;
}

uint64_t k_user_preempt_count(void) { return g_preempt; }

/* Запустить все зарегистрированные процессы кооперативно до конца. */
int64_t k_user_sched_run(void) {
    if (g_up_count == 0) return 0;
    g_up_cur = -1;
    int i = up_pick_next();
    if (i < 0) return 0;
    kernel_cr3 = rd_cr3();     /* ДО up_activate: иначе прочитаем CR3 процесса */
    user_done = 0;
    uint64_t f = up_activate(i);
    ulog_hx(g_up[i].pml4);
    u_putc(' ');
    extern int user_enter(uint64_t frame, uint64_t pml4);
    user_enter(f, g_up[i].pml4);
    if (kernel_cr3) wr_cr3(kernel_cr3);
    for (int k = 0; k < g_up_count; k++) {
        u_putc('#'); u_putc((char)('0' + k));
        u_putc(' '); ulog_hx(g_up[k].max_used); u_putc('\n');
        if (k == 0) { u_putc('p'); u_putc('r'); u_putc('e'); u_putc('e'); u_putc('m'); u_putc('p'); u_putc('t'); u_putc('='); ulog_hx(g_preempt); u_putc('\n'); }
        {   /* код завершения каждого процесса */
            const char* t = "userapp exit pid=";
            while (*t) u_putc(*t++);
            ulog_hx((uint64_t)g_up[k].pid);
            t = " code=";
            while (*t) u_putc(*t++);
            ulog_hx(g_up[k].code); u_putc('\n');
        }
        g_up[k].state = 3;
    }
    g_up_cur = -1;
    return 1;
}

/* boot-тест ring 3 (вызывается из kmain): гейт DPL3 + exec user-hello.elf.
   Возвращает 1, если пользовательская программа напечатала маркер и вышла. */
extern int isr_syscall(void);

int64_t k_user_boot_test(void) {
    k_user_gdt_install();
    if (k_user_syscall_gate((int64_t)(uintptr_t)(void*)isr_syscall) != 1) return 0;
    int64_t e = k_user_exec_vfs("user-hello.elf");
    if (e <= 0) return 0;
    k_user_run(e);
    return 1;   /* пользователь отработал и вышел через sys_exit */
}

/* IDT-гейт 0x80 c DPL=3 (int из ring3) */
int64_t k_user_syscall_gate(int64_t handler) {
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr;
    __asm__ __volatile__("sidt %0" : : "m"(idtr));
    uint64_t base = idtr.base;
    uint8_t* gate = (uint8_t*)(uintptr_t)(base + 0x80 * 16);
    int64_t rc = k_intr_set_gate(0x80, handler);
    gate[5] = (uint8_t)(gate[5] | 0x60);   /* DPL=3 */
    return 1;
}

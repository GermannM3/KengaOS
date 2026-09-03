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
            uint64_t foff = ph.offset + (page - va);
            uint64_t inoff = page == va ? (ph.vaddr & 0xFFFull) : 0;
            uint64_t cp = PAGE_SIZE - inoff;
            if (foff < size) {
                if (foff + cp > size) cp = size - foff;
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


void k_syscall_handler(void* frame_v) {
    uint64_t* f = (uint64_t*)frame_v;
    /* frame: rax,rbx,rcx,rdx,rsi,rdi,rbp,r8..r15,vector,error,rip,cs,rflags,rsp,ss */
    uint64_t num = f[0];
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
        user_done = 1;
        f[0] = 0;
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

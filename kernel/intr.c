/* intr.c — GDT + IDT + exception handling + panic (M2.1).
 *
 * A flat 64-bit GDT (kernel code/data), an IDT whose 256 entries point at the
 * isr_N stubs in isr.S, and a common handler that prints a readable panic to
 * both UART and the framebuffer before halting. Exposed to the Kenga kernel
 * via FFI: k_kf_intr_init(), k_kf_intr_test().
 */
#include "kf_rt.h"

/* isr_N stub addresses (defined in isr.S). */
extern void isr_0(void);
extern void isr_1(void);
extern void isr_2(void);
extern void isr_3(void);
extern void isr_4(void);
extern void isr_5(void);
extern void isr_6(void);
extern void isr_7(void);
extern void isr_8(void);
extern void isr_9(void);
extern void isr_10(void);
extern void isr_11(void);
extern void isr_12(void);
extern void isr_13(void);
extern void isr_14(void);
extern void isr_15(void);
extern void isr_16(void);
extern void isr_17(void);
extern void isr_18(void);
extern void isr_19(void);
extern void isr_20(void);
extern void isr_21(void);
extern void isr_22(void);
extern void isr_23(void);
extern void isr_24(void);
extern void isr_25(void);
extern void isr_26(void);
extern void isr_27(void);
extern void isr_28(void);
extern void isr_29(void);
extern void isr_30(void);
extern void isr_31(void);

#define ISR(n) isr_##n
#define ISR_ADDR(n) ((uint64_t)(uintptr_t)ISR(n))

typedef struct {
    uint16_t limit;
    uint64_t base __attribute__((packed));
} __attribute__((packed)) gdtr_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

/* 64-bit IDT gate (16 bytes). */
typedef struct {
    uint16_t lo;      /* offset bits 0-15     */
    uint16_t sel;     /* segment selector     */
    uint8_t  ist;     /* IST (0)              */
    uint8_t  flags;   /* type_attr            */
    uint16_t mid;     /* offset bits 16-31    */
    uint32_t hi;      /* offset bits 32-63    */
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vector, error;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
} kf_regs_t;

/* --- UART helpers (dup of kernel side, keeps intr.c self-contained) --- */
static void uputc(char c) { __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0x3F8)); }
static void uputs(const char* s) { if (!s) return; while (*s) uputc(*s++); }
static void uputnl(void) { uputc('\r'); uputc('\n'); }

static void uputhex(uint64_t v) {
    const char* h = "0123456789abcdef";
    uputs("0x");
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int d = (int)((v >> i) & 0xF);
        if (d || started || i == 0) { uputc(h[d]); started = 1; }
    }
}

/* --- GDT (flat 64-bit: null, kernel code, kernel data) --- */
static uint64_t gdt[3];

static void gdt_install(void) {
    gdt[0] = 0;                                       /* null */
    gdt[1] = 0x00AF9A000000FFFFULL;                   /* kernel code, sel 0x08 */
    gdt[2] = 0x00AF92000000FFFFULL;                   /* kernel data, sel 0x10 */
    gdtr_t gdtr;
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)(uintptr_t)gdt;
    __asm__ __volatile__("lgdt %0" : : "m"(gdtr));
    /* reload segments */
    __asm__ __volatile__(
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw $0x08, %%ax\n\t"
        "pushq %%rax\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        : : : "rax", "memory");
}

/* --- IDT --- */
static idt_entry_t idt[256];

uint16_t kernel_cs = 0x08;   /* actual Limine kernel CS, read at boot */

static void idt_set(int n, uint64_t off) {
    idt[n].lo    = (uint16_t)(off & 0xFFFF);
    idt[n].sel   = kernel_cs;   /* match the kernel's actual CS selector */
    idt[n].ist   = 0;
    idt[n].flags = 0x8E;        /* present, DPL0, 64-bit interrupt gate */
    idt[n].mid   = (uint16_t)((off >> 16) & 0xFFFF);
    idt[n].hi    = (uint32_t)((off >> 32) & 0xFFFFFFFF);
    idt[n].zero  = 0;
}

static void idt_install(void) {
    uint64_t offs[32] = {
        ISR_ADDR(0), ISR_ADDR(1), ISR_ADDR(2), ISR_ADDR(3),
        ISR_ADDR(4), ISR_ADDR(5), ISR_ADDR(6), ISR_ADDR(7),
        ISR_ADDR(8), ISR_ADDR(9), ISR_ADDR(10), ISR_ADDR(11),
        ISR_ADDR(12), ISR_ADDR(13), ISR_ADDR(14), ISR_ADDR(15),
        ISR_ADDR(16), ISR_ADDR(17), ISR_ADDR(18), ISR_ADDR(19),
        ISR_ADDR(20), ISR_ADDR(21), ISR_ADDR(22), ISR_ADDR(23),
        ISR_ADDR(24), ISR_ADDR(25), ISR_ADDR(26), ISR_ADDR(27),
        ISR_ADDR(28), ISR_ADDR(29), ISR_ADDR(30), ISR_ADDR(31),
    };
    for (int i = 0; i < 32; i++) idt_set(i, offs[i]);
    /* vectors 32..255: point at isr_0 as a dead-ish gate (not used yet). */
    for (int i = 32; i < 256; i++) idt_set(i, ISR_ADDR(0));

    idtr_t idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)(uintptr_t)idt;
    __asm__ __volatile__("lidt %0" : : "m"(idtr));
}

static void dump_cs(void) {
    __asm__ __volatile__("mov %%cs, %0" : "=r"(kernel_cs));
    uputs("kernel cs=");
    uputhex((uint64_t)kernel_cs);
    uputnl();
}

/* --- Panic: print to UART + framebuffer, then halt --- */
static const char* vector_name(int v) {
    switch (v) {
        case 0: return "DIVIDE BY ZERO (#DE)";
        case 3: return "BREAKPOINT (#BP)";
        case 6: return "INVALID OPCODE (#UD)";
        case 8: return "DOUBLE FAULT (#DF)";
        case 13: return "GENERAL PROTECTION (#GP)";
        case 14: return "PAGE FAULT (#PF)";
        case 11: return "SEGMENT NOT PRESENT (#NP)";
        case 12: return "STACK SEGMENT FAULT (#SS)";
        default: return "EXCEPTION";
    }
}

void k_kf_intr_handler(void* regs) {
    kf_regs_t* r = (kf_regs_t*)regs;

    /* INT3 is a recoverable test fault: prove the full IDT round-trip works
       (fault -> stub -> handler -> iretq -> continue) without halting. */
    if (r->vector == 3) {
        uputs("\r\n[KengaOS] INT3 CAUGHT (IDT ok)\r\n");
        return;
    }

    uint64_t cr2 = 0;
    if (r->vector == 14) {
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    }

    uputs("\r\n");
    uputs("[KengaOS] KERNEL PANIC\r\n");
    uputs("  vector: "); uputhex(r->vector); uputs("  "); uputs(vector_name(r->vector)); uputnl();
    uputs("  error : "); uputhex(r->error); uputnl();
    uputs("  rip   : "); uputhex(r->rip); uputnl();
    uputs("  cs    : "); uputhex(r->cs); uputnl();
    uputs("  flags : "); uputhex(r->rflags); uputnl();
    if (r->vector == 14) { uputs("  cr2   : "); uputhex(cr2); uputnl(); }

    /* Try to reflect it on the framebuffer too. */
    int64_t fb = k_kf_get_framebuffer();
    if (k_fb_init(fb) == 1) {
        k_fb_fill(0x1a0b0b);
        k_fb_text(24, 40, 0xffffff, 0x1a0b0b, "KERNEL PANIC");
        k_fb_text(24, 60, 0xf85149, 0x1a0b0b, "vector: ");
        k_fb_text(88, 60, 0xf85149, 0x1a0b0b, vector_name(r->vector));
        k_fb_text(24, 80, 0xe6edf3, 0x1a0b0b, "halting system");
    }

    uputs("\r\n[KengaOS] HALT\r\n");
    for (;;) { __asm__ __volatile__("cli; hlt"); }
}

static void enable_sse(void) {
    uint64_t cr4, cr0;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);   /* OSFXSR */
    cr4 |= (1ULL << 10);  /* OSXMMEXCPT */
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4));
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);  /* clear EM */
    cr0 |=  (1ULL << 1);  /* set MP */
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
}

int64_t k_intr_init(void) {
    /* Limine already sets up a working GDT; we rely on its kernel code/data
       segments. Use the actual current CS for IDT gates (not a hardcoded 0x08). */
    dump_cs();
    enable_sse();   /* -O2 emits SSE; needed or xorps -> #UD */
    idt_install();
    return 1;
}

/* Replace an IDT gate at runtime (e.g. point vector 32 at the timer ISR).
   The IDTR already points at the idt[] array, so writes take effect at once. */
int64_t k_intr_set_gate(int64_t n, int64_t off) {
    if (n < 0 || n >= 256) return 0;
    idt_set((int)n, (uint64_t)off);
    return 1;
}

/* Trigger a breakpoint (#BP, vector 3) to prove the IDT catches faults. */
int64_t k_intr_test(void) {
    __asm__ __volatile__("int3");
    return 0;
}

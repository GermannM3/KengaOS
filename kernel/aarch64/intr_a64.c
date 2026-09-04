/* intr_a64.c — исключения/прерывания aarch64: VBAR_EL1 + GICv2 (QEMU virt).
 *
 * aarch64-аналог intr.c: тот же контракт FFI (k_intr_init/k_intr_test),
 * та же паника в UART + framebuffer, тот же тест круглого пути
 * (на x86 — INT3, здесь — BRK: фолт -> вектор -> обработчик -> eret -> дальше).
 *
 * GICv2 на QEMU virt: дистрибьютор 0x08000000, интерфейс 0x08010000.
 * Виртуальный generic timer — PPI 27 (INTID 16+11).
 */
#include "kf_rt.h"

extern int64_t k_kf_get_hhdm(void);
/* --- QEMU virt: GICv2 --- */
#define GICD_BASE 0x08000000ull
#define GICC_BASE 0x08010000ull
#define GICD_CTLR      (GICD_BASE + 0x000)
#define GICD_TYPER     (GICD_BASE + 0x004)
#define GICD_ISENABLER (GICD_BASE + 0x100)
#define GICD_ICENABLER (GICD_BASE + 0x180)
#define GICD_ICPENDR   (GICD_BASE + 0x280)
#define GICD_IPRIORITYR (GICD_BASE + 0x400)
#define GICC_CTLR      (GICC_BASE + 0x000)
#define GICC_PMR       (GICC_BASE + 0x004)
#define GICC_IAR       (GICC_BASE + 0x00C)
#define GICC_EOIR      (GICC_BASE + 0x010)

#define IRQ_VTIMER 27   /* PPI: virtual timer (QEMU virt) */

/* --- GICv3 (SM6125 и QEMU gic-version=3): GICR CPU0 на virt --- */
#define GICR_BASE_VA   0x080A0000ull   /* identity-map PA (dev-map окно) */
#define GICR_CTLR      0x0000
#define GICR_WAKER     0x0014
#define GICR_IGROUPR0  0x0080
#define GICR_ISENABLER 0x0100
#define GICR_IPRIORITYR 0x0400

static int gic_version = 2;   /* заполняется в k_intr_init */
static int gic_sre_ok = 0;    /* DEBUG: ICC_SRE_EL1.SRE применился */

/* GICv3 sysreg-доступ: ICC_IAR1_EL1 = S3_0_C12_C12_0, ICC_EOIR1_EL1 = S3_0_C12_C12_1 */

/* --- UART (hw_a64.c) --- */
void k_arch_uart_puts(const char* s);
void k_arch_uart_putc(char c);
static void uputhex(uint64_t v) {
    const char* h = "0123456789abcdef";
    k_arch_uart_puts("0x");
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int d = (int)((v >> i) & 0xF);
        if (d || started || i == 0) { k_arch_uart_putc(h[d]); started = 1; }
    }
}

/* --- фрейм: ровно раскладка из vectors.S --- */
typedef struct {
    uint64_t rsvd[2];   /* +0   — выравнивание стека */
    uint64_t x[31];     /* +16  — x0..x30            */
    uint64_t elr;       /* +264 */
    uint64_t spsr;      /* +272 */
    uint64_t esr;       /* +280 */
    uint64_t far;       /* +288 */
} a64_frame;

extern int64_t k_fb_fill(int64_t color);
extern int64_t k_fb_text(int64_t x, int64_t y, int64_t fg, int64_t bg, const char* s);
int64_t k_kf_get_framebuffer(void);
void k_timer_rearm(void);   /* time_a64.c: перезарядка CNTV */

static void panic_reflect_fb(const char* why) {
    /* Попытка отразить панику на framebuffer (как на x86). */
    int64_t fb = k_kf_get_framebuffer();
    if (fb && k_fb_init(fb) == 1) {
        k_fb_fill(0x1a0b0b);
        k_fb_text(24, 40, 0xffffff, 0x1a0b0b, "KERNEL PANIC (aarch64)");
        k_fb_text(24, 60, 0xf85149, 0x1a0b0b, why);
        k_fb_text(24, 80, 0xe6edf3, 0x1a0b0b, "halting system");
    }
}

static void panic_halt(void) {
    k_arch_uart_puts("\r\n[KengaOS] HALT\r\n");
    for (;;) {
        k_arch_irq_disable();
        k_arch_hlt();
    }
}

void k_arch_hlt(void) {
    __asm__ __volatile__("wfi");
}

void k_arch_irq_enable(void) {
    __asm__ __volatile__("msr daifclr, #2");
}

void k_arch_irq_disable(void) {
    __asm__ __volatile__("msr daifset, #2");
}

void k_arch_halt_forever(void) {
    for (;;) { k_arch_irq_disable(); k_arch_hlt(); }
}

/* Sync-исключение: BRK — восстановимый тест, остальное — паника. */
void k_a64_sync_c(a64_frame* f) {
    uint64_t ec = (f->esr >> 26) & 0x3F;
    if (ec == 0x3C) {   /* BRK */
        k_arch_uart_puts("\r\n[KengaOS] BRK CAUGHT (aarch64 vectors ok)\r\n");
        f->elr += 4;    /* продолжить после brk */
        return;
    }
    static const char* ec_name[] = {
        "UNKNOWN", "WFI/WFE TRAP", "ILLEGAL A32", "SVC A32",
        "PC ALIGN", "SP ALIGN", "SIMD/FLOAT TRAP", "LSM64",
        0, 0, "ILL-EXC-10", "ILL-EXC-11", "ILL-EXC-12", "ILL-EXC-13",
        "SVC64", "HLT64", 0, 0, "SERROR", "BP", "BPA", "STEP",
        "STEPA", "WATCHPT", "WATCHPA", "BKPT", "BRK"
    };
    k_arch_uart_puts("\r\n");
    k_arch_uart_puts("[KengaOS] KERNEL PANIC (sync exception)\r\n");
    k_arch_uart_puts("  EC    : ");
    uputhex(ec);
    k_arch_uart_puts(ec < sizeof(ec_name)/sizeof(ec_name[0]) && ec_name[ec]
                     ? ec_name[ec] : "EXCEPTION");
    k_arch_uart_puts("\r\n  ESR   : ");
    uputhex(f->esr);
    k_arch_uart_puts("\r\n  ELR   : ");
    uputhex(f->elr);
    k_arch_uart_puts("\r\n  FAR   : ");
    uputhex(f->far);
    k_arch_uart_puts("\r\n");
    panic_reflect_fb("sync exception (see UART)");
    panic_halt();
}

/* FIQ/SError/lower-EL сюда попадать не должны. */
void k_a64_dead_c(a64_frame* f) {
    k_arch_uart_puts("\r\n[KengaOS] KERNEL PANIC (unexpected vector: FIQ/SError/lower-EL)\r\n");
    k_arch_uart_puts("  ESR   : ");
    uputhex(f->esr);
    k_arch_uart_puts("\r\n  ELR   : ");
    uputhex(f->elr);
    k_arch_uart_puts("\r\n  FAR   : ");
    uputhex(f->far);
    k_arch_uart_puts("\r\n");
    panic_halt();
}

/* IRQ: читаем IAR, перезаряжаем таймер, тик, EOI. GICv2 — MMIO,
   GICv3 — системные регистры ICC_IAR1/EOIR1. */
void k_a64_irq_c(a64_frame* f) {
    (void)f;
    if (gic_version >= 3) {
        uint64_t iar;
        __asm__ __volatile__("mrs %0, icc_iar1_el1" : "=r"(iar));
        uint64_t id = iar & 0x3FFull;
        if (id == IRQ_VTIMER) {
            k_timer_rearm();    /* до EOI: снять уровень CNTV */
            k_timer_tick();
        }
        __asm__ __volatile__("msr icc_eoir1_el1, %0" : : "r"(iar));
        return;
    }
    uint32_t iar = mmio_read32(GICC_IAR);
    uint32_t id  = iar & 0x3FF;
    if (id == IRQ_VTIMER) {
        k_timer_rearm();    /* до EOI: снять уровень CNTV */
        k_timer_tick();
        mmio_write32(GICC_EOIR, iar);
        return;
    }
    if (id != 1023) {   /* spurious — просто EOI */
        mmio_write32(GICC_EOIR, iar);
    }
}

int64_t k_intr_init(void) {
    /* Таблица векторов. */
    extern void k_a64_vectors(void);
    uint64_t vb;
    __asm__ __volatile__("mrs %0, vbar_el1" : "=r"(vb));
    __asm__ __volatile__("msr vbar_el1, %0" : : "r"((uint64_t)(uintptr_t)k_a64_vectors));
    (void)vb;

    /* Версия GIC: ID_AA64PFR0_EL1[27:24] — 0 = v2, 1 = v3/4. */
    uint64_t pfr0;
    __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
#ifdef KENGA_GICV3
    gic_version = (int)((pfr0 >> 24) & 0xF) ? 3 : 2;   /* SM6125: v3 */
#else
    gic_version = 2;   /* проверенный путь (QEMU virt gic-version=2) */
#endif

    if (gic_version >= 3) {
        /* --- GICv3 (SM6125, QEMU gic-version=3) --- */
        /* ICC_SRE_EL1.SRE = 1 (sysreg-режим интерфейса) */
        uint64_t sre;
        __asm__ __volatile__("mrs %0, icc_sre_el1" : "=r"(sre));
        if (!(sre & 1)) {
            __asm__ __volatile__("msr icc_sre_el1, %0" : : "r"((uint64_t)1));
            __asm__ __volatile__("isb");
            __asm__ __volatile__("mrs %0, icc_sre_el1" : "=r"(sre));
        }
        gic_sre_ok = (sre & 1) ? 1 : 0;

        /* GICD: disable, затем enable Group1NS */
        mmio_write32(GICD_CTLR, 0);
        for (uint32_t i = 0; i < 32; i++) {
            mmio_write32(GICD_ICENABLER + 4 * i, 0xFFFFFFFFu);
            mmio_write32(GICD_ICPENDR + 4 * i, 0xFFFFFFFFu);
        }
        for (uint32_t i = 0; i < 128; i++) {   /* приоритет 0xA0 всем */
            mmio_write32(GICD_IPRIORITYR + 4 * i, 0xA0A0A0A0u);
        }
        mmio_write32(GICD_ISENABLER + 4 * (IRQ_VTIMER / 32), 1u << (IRQ_VTIMER % 32));
        mmio_write32(GICD_CTLR, 2u);           /* EnableG1NS */

        /* GICR CPU0 (virt: 0x080A0000): разбудить редистрибьютор.
           MMIO доступ через identity-map (raw PA), как GICv2. */
        volatile uint32_t* gicr = (volatile uint32_t*)(uintptr_t)(GICR_BASE_VA);
        for (int spin = 0; spin < 1000000; spin++) {
            if (!(gicr[GICR_WAKER / 4] & (1u << 1))) break;   /* ProcessorSleep=0 */
            gicr[GICR_WAKER / 4] &= ~(1u << 1);
        }
        gicr[GICR_IGROUPR0 / 4] = 0xFFFFFFFFu;            /* всё в Group1NS */
        gicr[GICR_ISENABLER / 4 + (IRQ_VTIMER / 32)] = 1u << (IRQ_VTIMER % 32);
        {
            volatile uint8_t* pr = (volatile uint8_t*)(uintptr_t)
                ((uintptr_t)gicr + GICR_IPRIORITYR + IRQ_VTIMER);
            *pr = 0xA0;
        }

        /* CPU-интерфейс: PMR + Group1 enable */
        __asm__ __volatile__("msr icc_pmr_el1, %0" : : "r"((uint64_t)0xFF));
        __asm__ __volatile__("msr icc_igrpen1_el1, %0" : : "r"((uint64_t)1));
        __asm__ __volatile__("isb");
        return 1;
    }

    /* --- GICv2: дистрибьютор + CPU-интерфейс (QEMU gic-version=2, RPi) --- */
    uint32_t typer = mmio_read32(GICD_TYPER);
    uint32_t nlines = (typer & 0x1F) + 1;   /* группы по 32 INTID */
    mmio_write32(GICD_CTLR, 0);
    for (uint32_t i = 0; i < nlines; i++) {
        mmio_write32(GICD_ICENABLER + 4 * i, 0xFFFFFFFFu);
        mmio_write32(GICD_ICPENDR + 4 * i, 0xFFFFFFFFu);
    }
    for (uint32_t i = 0; i < nlines * 4; i++) {   /* приоритет 0xA0 всем */
        mmio_write32(GICD_IPRIORITYR + 4 * i, 0xA0A0A0A0u);
    }
    mmio_write32(GICD_ISENABLER + 4 * (IRQ_VTIMER / 32), 1u << (IRQ_VTIMER % 32));
    mmio_write32(GICD_CTLR, 1);

    mmio_write32(GICC_PMR, 0xFF);   /* пропускать все приоритеты */
    mmio_write32(GICC_CTLR, 1);

    /* IRQ пока замаскированы (DAIF.I=1): снимет k_gui_init перед
       десктоп-циклом — как sti на x86. */
    return 1;
}

/* Тест круглого пути: brk -> вектор -> sync_entry -> elr+4 -> eret -> здесь. */
int64_t k_intr_test(void) {
    __asm__ __volatile__("brk #0");
    return 0;
}

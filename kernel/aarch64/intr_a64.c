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

/* IRQ: читаем IAR, перезаряжаем таймер, тик, EOI. */
void k_a64_irq_c(a64_frame* f) {
    (void)f;
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

    /* GICv2: дистрибьютор + CPU-интерфейс. */
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

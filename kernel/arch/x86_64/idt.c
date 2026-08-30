/*  KengaOS — IDT (Interrupt Descriptor Table) для x86_64.
    Реализованы:
      - 32 исключения CPU (DE, DF, NMI, BP, OF, BR, UD, NM, DF, TS, NP, SS, GP, PF, ...)
      - 16 IRQ через PIC (8259): IRQ0=timer, IRQ1=keyboard, ...
      - Обработчики написаны на ассемблере-обёртке (asm.S), вызывают общий isr_handler.
*/
#include "idt.h"
#include "io.h"
#include "../i18n/i18n.h"
#include "../drivers/uart.h"

/* 8-байтный шлюз прерывания (Interrupt Gate) для x86_64 */
struct idt_entry {
    u16 base_low;
    u16 selector;
    u8  ist;
    u8  flags;
    u16 base_mid;
    u32 base_high;
    u32 reserved;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

#define IDT_ENTRIES 256
static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr  idtr;

#define IDT_FLAG_PRESENT  0x80
#define IDT_FLAG_RING0    0x00
#define IDT_FLAG_INTGATE  0x0E   /* interrupt gate, 16-bit seg sel */

/* Обработчики исключений (32 шт.) — каждый в asm.S */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

/* IRQ0..15 (они же isr32..47) */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void *isr_handlers[32] = {
    isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,
    isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,
    isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,
    isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31
};
static void *irq_handlers[16] = {
    irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7,
    irq8,irq9,irq10,irq11,irq12,irq13,irq14,irq15
};

/* Имена исключений для диагностики */
static const char *const exc_names[32] = {
    "Деление на ноль","Отладка","NMI","Точка останова","Переполнение",
    "Выход за границы","Неверный опкод","Устройство недоступно","Двойная ошибка",
    "Сопроцессор","Неверный TSS","Сегмент отсутствует","Ошибка стека",
    "Общая защита","Ошибка страницы","Зарезервировано","x87 FPU",
    "Выравнивание","Проверка машины","SIMD","Виртуализация",
    "Зарезервировано","Зарезервировано","Зарезервировано","Зарезервировано",
    "Зарезервировано","Зарезервировано","Зарезервировано","Зарезервировано",
    "Безопасность","Зарезервировано","Зарезервировано"
};

static void idt_set_gate(int i, void *handler, u8 ist, u8 flags) {
    u64 addr = (u64)handler;
    idt[i].base_low    = addr & 0xFFFF;
    idt[i].selector    = 0x08;          /* code segment в GDT */
    idt[i].ist         = ist;
    idt[i].flags       = flags;
    idt[i].base_mid    = (addr >> 16) & 0xFFFF;
    idt[i].base_high   = (addr >> 32) & 0xFFFFFFFF;
    idt[i].reserved    = 0;
}

/* Имена исключений по номеру */
const char *idt_exception_name(u8 n) {
    if (n < 32) return exc_names[n];
    return "Неизвестно";
}

/* PIC 8259 remap: IRQ0..15 → ISR 32..47 */
#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1
#define PIC_EOI    0x20

static void pic_remap(void) {
    /* ICW1: инициализация, каскад, ICW4 нужен */
    outb(PIC1_CMD, 0x11);  io_wait();
    outb(PIC2_CMD, 0x11);  io_wait();
    /* ICW2: смещения векторов */
    outb(PIC1_DATA, 0x20); io_wait();   /* IRQ0..7  → 32..39 */
    outb(PIC2_DATA, 0x28); io_wait();   /* IRQ8..15 → 40..47 */
    /* ICW3: каскад */
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    /* ICW4: 8086 mode */
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    /* Маска: разрешены IRQ0 (таймер), IRQ1 (клавиатура) и IRQ2
       (каскад на PIC2 — без него НЕ работают IRQ8..15, включая мышь!). */
    outb(PIC1_DATA, 0xF8);   /* 11111000 — IRQ0, IRQ1, IRQ2 включены */
    outb(PIC2_DATA, 0xFF);   /* PIC2: всё выключено (мышь размаскирует драйвер) */
}

void idt_init(void) {
    pic_remap();

    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, isr_handlers[i], 0, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_INTGATE);
    }
    for (int i = 0; i < 16; i++) {
        idt_set_gate(32 + i, irq_handlers[i], 0, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_INTGATE);
    }

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (u64)&idt;
    __asm__ volatile ("lidt %0" :: "m"(idtr));
}

void pic_eoi(u8 irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

/* ============================================================
   Общий обработчик исключений — вызывается из asm-обёрток.

   Раскладка ДОЛЖНА совпадать с фактическим стеком isr_asm.S:
   asm пушит int_no, err_code, затем PUSH_REGS (r15 пушится
   последним → лежит по rsp+0). Ниже — кадр CPU (rip..ss).
   Регистр rdi в isr_asm.S указывает на r15 (низ стека).
   ============================================================ */
struct cpu_state {
    /* сохранённые регистры от asm-обёртки (в обратном порядке пуша) */
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no;
    u64 err_code;
    /* кадр прерывания CPU */
    u64 rip, cs, rflags, rsp, ss;
};

static irq_handler_t user_irq[16] = {0};

void irq_register(u8 irq_num, irq_handler_t handler) {
    if (irq_num < 16) user_irq[irq_num] = handler;
}

/* Точка входа из asm-обёрток (см. isr_asm.S) */
void isr_handler(struct cpu_state *state) {
    if (state->int_no < 32) {
        /* === Исключение CPU === */
        uart_puts(UART_COM1, "\r\n");
        uart_puts(UART_COM1, "!!! ИСКЛЮЧЕНИЕ CPU: ");
        /* выводим номер и имя — простой itoa */
        char buf[32];
        int n = state->int_no, p = 30;
        buf[p--] = 0;
        if (n == 0) buf[p--] = '0';
        while (n > 0) { buf[p--] = '0' + (n % 10); n /= 10; }
        uart_puts(UART_COM1, &buf[p+1]);
        uart_puts(UART_COM1, " (");
        uart_puts(UART_COM1, idt_exception_name((u8)state->int_no));
        uart_puts(UART_COM1, ") !!!\r\n");
        uart_puts(UART_COM1, "RIP=");
        /* упрощённый вывод rip — нам важно только что упало */
        uart_puts(UART_COM1, "(см. кадр)\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    } else if (state->int_no >= 32 && state->int_no < 48) {
        u8 irq = (u8)(state->int_no - 32);
        /* EOI ДО обработчика: handler может context_switch'нуться и не
           вернуться (например sched_tick при переключении на user-поток).
           Иначе PIC оставит IRQ "in service" и тикер умрёт навсегда. */
        pic_eoi(irq);
        if (user_irq[irq]) user_irq[irq](NULL);
    }
}

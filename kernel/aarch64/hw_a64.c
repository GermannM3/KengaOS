/* hw_a64.c — aarch64 "железный" слой: PL011 UART, 16550-порт-тень,
 * CPU-инфо (MIDR) вместо CPUID, boot-время вместо CMOS RTC.
 *
 * QEMU virt: UART0 — PL011 на 0x09000000.
 * Сгенерированный из kmain.kenga код общается с "UART 16550 на 0x3F8"
 * через k_arch_io_inb/outb (sed-слой в build-a64.sh переписывает x86-asm
 * на эти вызовы). Тень честно эмулирует то, что kmain ждёт от 16550:
 * loopback-тест проходит, данные уходят в PL011, мусор инициализации — нет.
 */
#include "kf_rt.h"

/* --- PL011 (QEMU virt UART0) --- */
#define UART0      0x09000000ull
#define UART_DR    (UART0 + 0x000)
#define UART_FR    (UART0 + 0x018)
#define UART_IBRD  (UART0 + 0x024)
#define UART_FBRD  (UART0 + 0x028)
#define UART_LCRH  (UART0 + 0x02C)
#define UART_CR    (UART0 + 0x030)
#define UART_ICR   (UART0 + 0x044)

#define FR_TXFF (1u << 5)
#define FR_RXFE (1u << 4)

void k_arch_uart_init(void) {
    mmio_write32(UART_CR, 0);          /* выключить */
    mmio_write32(UART_ICR, 0x7FF);     /* сбросить прерывания */
    mmio_write32(UART_IBRD, 13);       /* 115200 при 24 МГц (QEMU не строг) */
    mmio_write32(UART_FBRD, 1);
    mmio_write32(UART_LCRH, 0x70);     /* 8N1 + FIFO */
    mmio_write32(UART_CR, 0x301);      /* RXE | TXE | UARTEN */
}

void k_arch_uart_putc(char c) {
    while (mmio_read32(UART_FR) & FR_TXFF) { }
    mmio_write32(UART_DR, (uint32_t)(uint8_t)c);
}

void k_arch_uart_puts(const char* s) {
    if (!s) return;
    while (*s) k_arch_uart_putc(*s++);
}

static int pl011_rx_empty(void) { return (mmio_read32(UART_FR) & FR_RXFE) != 0; }

/* --- тень 16550: портовые inb/outb из сгенерированного kmain.c ---
 *
 * kmain.kenga инициализирует UART как 16550 на 0x3F8 (порты 0x3F8..0x3FF).
 * Правила:
 *   - запись в LCR (0x3FB) с битом DLAB — режим конфигурации: в данные
 *     не форвардим (иначе в лог уйдёт мусор DLL/DLM-записей);
 *   - запись в THR (0x3F8) вне конфигурации — байт в PL011;
 *   - чтение RBR (0x3F8) в loopback-тесте возвращает последний записанный;
 *   - LSR (0x3FD) — всегда THRE (передатчик пуст) + RX ready если есть.
 */
static uint8_t shadow_thr = 0;
static int     dlab_mode = 0;

void k_arch_io_outb(uint16_t port, uint8_t v) {
    switch (port) {
        case 0x3F8:
            if (!dlab_mode) { k_arch_uart_putc((char)v); }
            shadow_thr = v;
            break;
        case 0x3FB:
            dlab_mode = (v & 0x80) != 0;
            break;
        default:
            break;  /* IER/DLM/FCR/MCR и прочие — игнорируем */
    }
}

uint8_t k_arch_io_inb(uint16_t port) {
    switch (port) {
        case 0x3F8:
            return shadow_thr;              /* loopback-тест */
        case 0x3FD:                         /* LSR */
            return (uint8_t)(0x20 | (pl011_rx_empty() ? 0 : 0x01));
        default:
            return 0;
    }
}

/* --- CPU-инфо: MIDR_EL1 вместо CPUID --- */
int64_t k_hw_cpu_vendor(char* out, int max) {
    const char* v = "ARM";
    int n = 0;
    while (v[n] && n < max - 1) { out[n] = v[n]; n++; }
    out[n] = 0;
    return n;
}

int64_t k_hw_cpu_brand(char* out, int max) {
    uint64_t midr;
    __asm__ __volatile__("mrs %0, midr_el1" : "=r"(midr));
    unsigned impl = (midr >> 24) & 0xFF;
    unsigned part = (midr >> 4) & 0xFFF;
    const char* imp = impl == 0x41 ? "ARM Ltd."
                    : impl == 0x51 ? "Qualcomm"
                    : impl == 0x53 ? "Samsung"
                    : impl == 0x4E ? "NVIDIA"
                    : "unknown";
    int n = 0;
    while (imp[n] && n < max - 1) { out[n] = imp[n]; n++; }
    if (n < max - 1) out[n++] = ' ';
    /* part -> "part 0xNNN" */
    const char* tail = "part 0x";
    for (const char* p = tail; *p && n < max - 1; p++) out[n++] = *p;
    const char* hx = "0123456789abcdef";
    char tmp[3]; int t = 0;
    unsigned u = part;
    if (u == 0) tmp[t++] = '0';
    while (u && t < 3) { tmp[t++] = hx[u & 0xF]; u >>= 4; }
    while (t-- && n < max - 1) out[n++] = tmp[t];
    out[n] = 0;
    return n;
}

static char brand_buf[48];
static char rtc_buf[16];

const char* k_hw_cpu_brand_str(void) {
    k_hw_cpu_brand(brand_buf, sizeof(brand_buf));
    return brand_buf;
}

/* RTC на QEMU virt нет (CMOS — это x86). Честно: boot-время от uptime. */
extern int64_t k_time_uptime_ms(void);

const char* k_hw_rtc_time_str(void) {
    int64_t s = k_time_uptime_ms() / 1000;
    int h = (int)(s / 3600), m = (int)((s / 60) % 60), sec = (int)(s % 60);
    rtc_buf[0] = (char)('0' + (h / 10) % 10);
    rtc_buf[1] = (char)('0' + h % 10);
    rtc_buf[2] = ':';
    rtc_buf[3] = (char)('0' + m / 10);
    rtc_buf[4] = (char)('0' + m % 10);
    rtc_buf[5] = ':';
    rtc_buf[6] = (char)('0' + sec / 10);
    rtc_buf[7] = (char)('0' + sec % 10);
    rtc_buf[8] = 0;
    return rtc_buf;
}

int64_t k_hw_rtc_str(char* out, int max) {
    const char* s = k_hw_rtc_time_str();
    int n = 0;
    while (s[n] && n < max - 1) { out[n] = s[n]; n++; }
    out[n] = 0;
    return n;
}

int64_t k_hw_cpu_flags(void) { return 1; }

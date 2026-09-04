/* fdt_boot.c — boot-тест FDT-парсера: грузит virt.dtb из initrd,
   печатает uart-базу и память из device tree. */
#include "kf_rt.h"

void k_arch_uart_puts(const char* s);
int64_t k_fdt_load(const char* name);
int64_t k_fdt_prop_u32(const char* node, const char* prop);
int64_t k_fdt_prop_str(const char* node, const char* prop, char* out, int max);
int64_t k_fdt_memory(uint64_t* base_out, uint64_t* size_out);
int64_t k_fdt_uart_base(void);
int64_t k_fdt_dbg(int64_t i);

static void phex(uint64_t v) {
    const char* h = "0123456789abcdef";
    char out[19]; int n = 0, started = 0;
    out[n++] = '0'; out[n++] = 'x';
    for (int i = 60; i >= 0; i -= 4) {
        int d = (int)((v >> i) & 0xF);
        if (d || started || i == 0) { out[n++] = h[d]; started = 1; }
    }
    out[n] = 0;
    k_arch_uart_puts(out);
}

int64_t k_fdt_boot_test(void) {
    if (k_fdt_load("virt.dtb") != 1) return 0;
    k_arch_uart_puts("fdt: loaded\n");
    uint64_t mb = 0, ms = 0;
    if (k_fdt_memory(&mb, &ms) == 1) {
        k_arch_uart_puts("fdt: mem=");
        phex(mb);
        k_arch_uart_puts("+");
        phex(ms);
        k_arch_uart_puts("\n");
    }
    int64_t ub = k_fdt_uart_base();
    k_arch_uart_puts("fdt: uart=");
    phex((uint64_t)ub);
    k_arch_uart_puts(" dbg=");
    k_arch_uart_puts(" scan=");
    k_arch_uart_puts("\n");
    char ba[128];
    int64_t bl = k_fdt_prop_str("chosen", "bootargs", ba, 128);
    if (bl >= 0) {
        k_arch_uart_puts("fdt: bootargs=");
        k_arch_uart_puts(ba);
        k_arch_uart_puts("\n");
    }
    return 1;
}

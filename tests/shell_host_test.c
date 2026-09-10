/* host-тест пайпов шелла: gcc -std=c17 -O1 tests/shell_host_test.c kernel/kf_shell.c -o test && ./test */
/* host-тест пайпов шелла: линкуем kf_shell.c с заглушками k_* */
#include <stdio.h>
#include <stdint.h>

/* заглушки символов, которых нет на хосте */
void k_fb_con_print(const char* s) { (void)s; }
void k_fb_con_clear(void) {}
void k_fb_con_putc(int c) { (void)c; }
void k_fb_con_redraw(void) {}
void k_fb_con_init(void) {}
int  k_kbd_pending(void) { return 0; }
int  k_kbd_read(void) { return 0; }
void k_kbd_init(void) {}
int64_t k_proc_count(void) { return 0; }
int64_t k_proc_pid_at(int64_t i) { (void)i; return 0; }
const char* k_proc_name_at(int64_t i) { (void)i; return ""; }
int64_t k_proc_parent_at(int64_t i) { (void)i; return 0; }
int64_t k_proc_caps(int64_t p) { (void)p; return 0; }
int64_t k_proc_set_caps(int64_t p, int64_t v) { (void)p; (void)v; return 0; }
void k_proc_init(void) {}
void k_proc_spawn(const char* n, void (*e)(void), int64_t c) { (void)n; (void)e; (void)c; }
int64_t k_ipc_send(int64_t p, const char* m) { (void)p; (void)m; return 1; }
int64_t k_ipc_recv_str(char* b, int n) { (void)n; b[0] = 0; return 0; }
int64_t k_logger_pid(void) { return 1; }
int64_t k_agent_pid(void) { return 2; }
int64_t k_model_pid(void) { return 3; }
int64_t k_vfs_count(void) { return 0; }
const char* k_vfs_name(int64_t i) { (void)i; return ""; }
int64_t k_vfs_cat(const char* n, char* b, int sz) { (void)n; (void)sz; b[0] = 0; return 0; }
uint64_t k_task_create(void (*e)(void)) { (void)e; return 0; }
uint64_t k_task_yield(void) { return 0; }
void k_power_reboot(void) {}
void k_power_shutdown(void) {}
int64_t k_hw_cpu_vendor(char* b, int s) { (void)s; b[0] = 0; return 0; }
int64_t k_hw_cpu_brand(char* b, int s) { (void)s; b[0] = 0; return 0; }
int64_t k_hw_rtc_str(char* b, int s) { (void)s; b[0] = 0; return 0; }
int64_t k_time_uptime_ms(void) { return 0; }
int64_t k_mem_free_bytes(void) { return 0; }
const char* dec(int64_t v) { static char b[24]; int t = 0, neg = 0; unsigned long long u = (unsigned long long)v; if (v < 0) { neg = 1; u = (unsigned long long)(-v); } if (!u) b[t++] = '0'; while (u) { b[t++] = (char)('0' + u % 10); u /= 10; } if (neg) b[t++] = '-'; for (int i = 0; i < t / 2; i++) { char c = b[i]; b[i] = b[t-1-i]; b[t-1-i] = c; } b[t] = 0; return b; }
int64_t k_mem_total_bytes(void) { return 0; }
int64_t k_mem_pages_free(void) { return 0; }
int64_t k_mem_region_count(void) { return 0; }
int64_t k_mem_region_type(int64_t i) { (void)i; return 0; }
int64_t k_mem_region_base(int64_t i) { (void)i; return 0; }
int64_t k_mem_region_len(int64_t i) { (void)i; return 0; }

int64_t k_shell_pipe_selftest(void);

int main(void) {
    int ok = (int)k_shell_pipe_selftest();
    printf("HOST PIPE %s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

#ifndef KENGA_FREESTANDING_H
#define KENGA_FREESTANDING_H

/*
 * kf_rt.h — kernel-side hooks for the Kenga emit-c --freestanding runtime.
 *
 * The generated kmain.c already carries the full freestanding runtime
 * (RUNTIME_FS in src/codegen.rs): _k_memcpy/_k_memset/_k_strlen/_k_mmio_*,
 * k_die, and the weak allocator chain (kf_alloc -> __builtin_malloc -> k_die).
 * This header therefore does NOT redefine memcpy/memset/strlen/strcmp macros —
 * doing so would collide with the runtime's own definitions.
 *
 * It only declares the FFI symbols the kernel provides:
 *   - kf_alloc(size_t)          — real allocator (optional; weak hook).
 *   - k_kf_get_boot_info(void)  — limine_bootloader_info_response* (Limine v12),
 *                                 from start.S (.limine_requests section).
 *   - k_kf_str(i64)             — cast an address to const char* (for UART).
 * Kenga source names are mangled with a k_ prefix by emit-c, hence the
 * doubled k_ on the exported C symbols.
 */

#include <stdint.h>
#include <stddef.h>

#ifndef __cplusplus
typedef _Bool bool;
#define true 1
#define false 0
#endif

/* --- FFI hooks (provided by the kernel / start.S) --- */

/* Weak hook consulted by the runtime's k_alloc chain before __builtin_malloc.
   Provide a strong definition (e.g. kmalloc/buddy allocator) in kernel C code. */
void* kf_alloc(size_t n);

/* struct limine_bootloader_info_response* (Limine v12) passed by start.S. */
int64_t k_kf_get_boot_info(void);

/* First limine_framebuffer* (Limine v12), or 0 if none — from start.S. */
int64_t k_kf_get_framebuffer(void);

/* HHDM offset (Limine v12), or 0 if none — from start.S. */
int64_t k_kf_get_hhdm(void);

/* Limine memory map response* — from start.S. */
int64_t k_kf_get_memmap(void);

/* Limine modules response* — from start.S. */
int64_t k_kf_get_modules(void);

/* Physical memory / heap (kernel/kf_mem.c). */
int64_t k_mem_init(void);
int64_t k_mem_free_bytes(void);
int64_t k_mem_total_bytes(void);
int64_t k_mem_palloc(void);
int64_t k_mem_pfree(int64_t addr);
int64_t k_mem_pages_free(void);
int64_t k_mem_region_count(void);
int64_t k_mem_region_base(int64_t i);
int64_t k_mem_region_len(int64_t i);
int64_t k_mem_region_type(int64_t i);

/* Power (kernel/kf_power.c). */
int64_t k_power_reboot(void);
int64_t k_power_shutdown(void);

/* Model (kernel/kf_model.c): tiny in-kernel MLP. */
int64_t k_model_init(void);
int64_t k_model_infer(int64_t a, int64_t b);
int64_t k_model_dbg(void);

/* Hardware info (kernel/kf_hw.c). */
int64_t k_hw_cpu_vendor(char* out, int max);
int64_t k_hw_cpu_brand(char* out, int max);
const char* k_hw_cpu_brand_str(void);
const char* k_hw_rtc_time_str(void);
int64_t k_hw_cpu_flags(void);
int64_t k_hw_rtc_str(char* out, int max);

/* Print an int64 to the UART (COM1, port 0x3F8) as decimal. */
int64_t k_kf_puti(int64_t n);

/* Interrupt core (kernel/intr.c): install GDT+IDT and catch exceptions. */
int64_t k_intr_init(void);
int64_t k_intr_test(void);
int64_t k_intr_set_gate(int64_t n, int64_t off);
void k_kf_intr_handler(void* regs);
extern uint16_t kernel_cs;

/* Scheduler (kernel/sched.c): round-robin multitasking. */
int64_t k_sched_init(void);
uint64_t k_sched_yield(uint64_t current_ctx);

/* Mouse (kernel/kf_mouse.c): PS/2 IRQ12 -> cursor + buttons. */
int64_t k_mouse_init(void);
int64_t k_mouse_x(void);
int64_t k_mouse_y(void);
int64_t k_mouse_buttons(void);
void k_mouse_irq(void);

/* Keyboard (kernel/kf_kbd.c): PS/2 IRQ1 -> ring buffer. */
int64_t k_kbd_init(void);
int64_t k_kbd_pending(void);
int64_t k_kbd_read(void);
void k_kbd_irq(void);

/* Framebuffer text console (kernel/kf_fb.c). */
int64_t k_fb_con_init(void);
int64_t k_fb_con_print(const char* s);
void k_fb_con_putc(int64_t c);
void k_fb_con_clear(void);
void k_fb_con_dump(void);
int64_t k_fb_con_redraw(void);

/* Shell (kernel/kf_shell.c). */
int64_t k_shell_init(void);

/* GUI desktop (kernel/kf_gui.c). */
int64_t k_gui_init(void);

/* Kenga-written desktop chrome (kernel/ui.kenga, compiled via kmain.kenga).
   Adaptive: takes the real framebuffer w/h plus computed panel sizes. */
int64_t k_ui_chrome(int64_t w, int64_t h, int64_t sb, int64_t tb, int64_t stb,
                    int64_t count, int64_t up, int64_t mx, int64_t my, int64_t cur);

/* Kenga-written desktop (kernel/desktop.kenga, compiled via kmain.kenga). */
int64_t k_desktop_main(void);

uint64_t k_task_create(void (*entry)(void));
uint64_t k_task_yield(void);
uint64_t k_sched_current(void);
int64_t k_yield_agent(void);

/* Processes + IPC (kernel/kf_proc.c). */
#define CAP_IPC          (1ull << 0)
#define CAP_SPAWN        (1ull << 1)
#define CAP_POWER        (1ull << 2)
#define CAP_MEM          (1ull << 3)
#define CAP_MODEL_INFER  (1ull << 4)
#define CAP_MODEL_LOAD   (1ull << 5)
#define CAP_MODEL_TRAIN  (1ull << 6)
#define CAP_UI           (1ull << 7)
#define CAP_ALL    (CAP_IPC|CAP_SPAWN|CAP_POWER|CAP_MEM|CAP_MODEL_INFER|CAP_MODEL_LOAD|CAP_MODEL_TRAIN|CAP_UI)
#define CAP_AGENT  (CAP_IPC|CAP_SPAWN)

int64_t k_proc_init(void);
int64_t k_proc_spawn(const char* name, void (*entry)(void), uint64_t caps);
int64_t k_proc_caps(int64_t pid);
int64_t k_proc_qlen(int64_t pid);
int64_t k_proc_set_caps(int64_t pid, int64_t caps);
int64_t k_ipc_send(int64_t pid, const char* data);
int64_t k_ipc_recv_str(char* buf, int max);
int64_t k_ipc_poll(void);
int64_t k_proc_count(void);
int64_t k_proc_pid_at(int64_t idx);
int64_t k_proc_parent_at(int64_t idx);
const char* k_proc_name_at(int64_t idx);
int64_t k_logger_pid(void);
int64_t k_agent_pid(void);
int64_t k_model_pid(void);
int64_t k_researcher_pid(void);

/* Agent-created windows (CAP_UI). */
int64_t k_ui_register_window(const char* title, const char* text);
int64_t k_ui_system_window(const char* title, const char* text);
int64_t k_ui_window_count(void);
int64_t k_ui_window_x(int64_t idx);
int64_t k_ui_window_y(int64_t idx);
int64_t k_ui_window_w(int64_t idx);
int64_t k_ui_window_h(int64_t idx);
int64_t k_ui_window_z(int64_t idx);
const char* k_ui_window_title(int64_t idx);
const char* k_ui_window_text(int64_t idx);
int64_t k_ui_window_front(int64_t idx);
int64_t k_ui_window_move(int64_t idx, int64_t x, int64_t y);
int64_t k_ui_window_set_text(int64_t idx, const char* text);
int64_t k_ui_window_close(int64_t idx);
int64_t k_ui_input_putc(int64_t c);
int64_t k_ui_input_clear(void);
const char* k_ui_input_str(void);
int64_t k_ui_input_len(void);
int64_t k_ui_input_submit(void);
int64_t k_ui_log(const char* s);
const char* k_ui_log_at(int64_t i);
const char* dec(int64_t n);

/* Timer / uptime (kernel/kf_time.c). */
int64_t k_timer_init(void);
int64_t k_time_uptime_ms(void);
void k_timer_tick(void);

/* Virtual filesystem (kernel/kf_vfs.c). */
int64_t k_vfs_count(void);
const char* k_vfs_name(int64_t idx);
int64_t k_vfs_cat(const char* name, char* out, int max);
int64_t k_vfs_init_rd(int64_t addr, int64_t size);
int64_t k_vfs_version_addr(void);

/* Framebuffer FFI (implemented in kernel/kf_fb.c): init from a
   limine_framebuffer*, then draw pixels/rects/text into the linear framebuffer. */
int64_t k_fb_init(int64_t fb);
int64_t k_fb_ready(void);
int64_t k_fb_width(void);
int64_t k_fb_height(void);
int64_t k_fb_putpixel(int64_t x, int64_t y, int64_t color);
int64_t k_fb_getpixel(int64_t x, int64_t y);
int64_t k_fb_fill(int64_t color);
void k_fb_fill_rect(int64_t x0, int64_t y0, int64_t w, int64_t h, int64_t color);
int64_t k_fb_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);
int64_t k_fb_hrect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);
int64_t k_fb_text(int64_t x, int64_t y, int64_t fg, int64_t bg, const char* s);
int64_t k_fb_xor(int64_t x, int64_t y, int64_t color);
int64_t k_fb_cursor(int64_t x, int64_t y);

/* Turn a numeric address into a C string pointer (for uart_puts on MMIO/tag
   strings living at raw addresses). */
static inline const char* k_kf_str(int64_t addr) {
    return (const char*)(uintptr_t)addr;
}

/* --- plain helpers for kernel-side C (no macro aliases; runtime has its own) --- */

static inline void* kf_memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline void* kf_memset(void* dst, int c, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    unsigned char v = (unsigned char)c;
    for (size_t i = 0; i < n; i++) d[i] = v;
    return dst;
}

static inline int kf_memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static inline size_t kf_strlen(const char* s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static inline int kf_strcmp(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

#define MMIO_READ(T, addr)  (*(volatile T*)(uintptr_t)(addr))
#define MMIO_WRITE(T, addr, val)  (*(volatile T*)(uintptr_t)(addr) = (val))

static inline uint8_t  mmio_read8 (uintptr_t a) { return MMIO_READ(uint8_t,  a); }
static inline uint16_t mmio_read16(uintptr_t a) { return MMIO_READ(uint16_t, a); }
static inline uint32_t mmio_read32(uintptr_t a) { return MMIO_READ(uint32_t, a); }
static inline uint64_t mmio_read64(uintptr_t a) { return MMIO_READ(uint64_t, a); }

static inline void mmio_write8 (uintptr_t a, uint8_t  v) { MMIO_WRITE(uint8_t,  a, v); }
static inline void mmio_write16(uintptr_t a, uint16_t v) { MMIO_WRITE(uint16_t, a, v); }
static inline void mmio_write32(uintptr_t a, uint32_t v) { MMIO_WRITE(uint32_t, a, v); }
static inline void mmio_write64(uintptr_t a, uint64_t v) { MMIO_WRITE(uint64_t, a, v); }

#define K_ASM(arch, code) __asm__ __volatile__(code)

static inline void kf_halt(void) { __asm__ __volatile__("hlt"); }
static inline void kf_cli(void)  { __asm__ __volatile__("cli"); }
static inline void kf_sti(void)  { __asm__ __volatile__("sti"); }

#endif
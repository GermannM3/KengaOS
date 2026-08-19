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

/* Framebuffer FFI (implemented in kernel/kf_fb.c): init from a
   limine_framebuffer*, then draw pixels/rects/text into the linear framebuffer. */
int64_t k_fb_init(int64_t fb);
int64_t k_fb_ready(void);
int64_t k_fb_width(void);
int64_t k_fb_height(void);
int64_t k_fb_putpixel(int64_t x, int64_t y, int64_t color);
int64_t k_fb_fill(int64_t color);
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
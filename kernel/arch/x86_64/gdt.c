/*  KengaOS — GDT для x86_64 long mode.
    Раскладка подогнана под SYSCALL/SYSRET:
      SYSCALL: CS = STAR[47:32] = 0x08, SS = 0x10
      SYSRET : CS = STAR[63:48]+16 = 0x28, SS = STAR[63:48]+8 = 0x20
    Поэтому user-сегменты должны быть на индексах 4 (SS) и 5 (CS):
      0 — null
      1 — code ring 0 (0x08)
      2 — data ring 0 (0x10)
      3 — data ring 3 (0x18, не используется)
      4 — data ring 3 (0x20 → SYSRET SS 0x23)
      5 — code ring 3 (0x28 → SYSRET CS 0x2B)
      6 — TSS (0x30)
*/
#include "gdt.h"
#include "io.h"
#include "../lib/types.h"

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  limit_high_flags;
    u8  base_high;
} __attribute__((packed));

struct gdt_tss_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  limit_high_flags;
    u8  base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

struct tss_entry {
    u32 reserved0;
    u64 rsp[3];      /* RSP0/1/2 */
    u64 reserved1;
    u64 ist[8];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed));

static struct tss_entry      tss       __attribute__((aligned(16)));
static struct gdt_ptr        gdtr;

/* GDT как один смежный массив: 6 обычных записей (8 б) + TSS-дескриптор (16 б).
   Отдельная статик-переменная для TSS-дескриптора не годится:
   её поля только пишутся, и clang -O2 выкидывает её (dead stores),
   из-за чего ltr получает мусорный дескриптор (#GP). */
static u8 gdt_table[6 * 8 + 16] __attribute__((aligned(16)));
#define GDT_ENTRY(i)  ((struct gdt_entry *)     (gdt_table + (i) * 8))
#define GDT_TSS_DESC  ((struct gdt_tss_entry *) (gdt_table + 6 * 8))

/* access bytes */
#define GDT_ACCESS_CODE_RING0  0x9A
#define GDT_ACCESS_DATA_RING0  0x92
#define GDT_ACCESS_CODE_RING3  0xFA   /* present, ring3, code, executable, readable */
#define GDT_ACCESS_DATA_RING3  0xF2   /* present, ring3, data, writable */
#define GDT_ACCESS_TSS         0x89
#define GDT_FLAGS_LONG         0xA0

static void gdt_set_entry(int i, u8 access, u8 flags) {
    struct gdt_entry *e = GDT_ENTRY(i);
    e->limit_low       = 0;
    e->base_low        = 0;
    e->base_mid        = 0;
    e->access          = access;
    e->limit_high_flags= flags;
    e->base_high       = 0;
}

static void gdt_load(struct gdt_ptr *g) {
    __asm__ volatile ("lgdt %0" :: "m"(*g));
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "mov $0x08, %%ax\n"
        "push %%rax\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        ::: "rax"
    );
}

static void tss_flush(u16 sel) {
    __asm__ volatile ("ltr %0" :: "r"(sel));
}

void gdt_init(void) {
    for (int i = 0; i < (int)sizeof(tss); i++) ((u8*)&tss)[i] = 0;
    tss.iomap_base = sizeof(tss);

    /* Обнулить все 6 записей */
    for (int i = 0; i < 6; i++) {
        gdt_set_entry(i, 0, 0);
    }
    gdt_set_entry(1, GDT_ACCESS_CODE_RING0, GDT_FLAGS_LONG);
    gdt_set_entry(2, GDT_ACCESS_DATA_RING0, GDT_FLAGS_LONG);
    gdt_set_entry(3, GDT_ACCESS_DATA_RING3, GDT_FLAGS_LONG);  /* не используется */
    gdt_set_entry(4, GDT_ACCESS_DATA_RING3, GDT_FLAGS_LONG);  /* user SS (SYSRET) */
    gdt_set_entry(5, GDT_ACCESS_CODE_RING3, GDT_FLAGS_LONG);  /* user CS (SYSRET) */

    /* TSS — дескриптор в конце таблицы (индекс 6, селектор 0x30) */
    u64 tss_base = (u64)&tss;
    struct gdt_tss_entry *tssd = GDT_TSS_DESC;
    tssd->limit_low       = sizeof(tss) - 1;
    tssd->base_low        = tss_base & 0xFFFF;
    tssd->base_mid        = (tss_base >> 16) & 0xFF;
    tssd->access          = GDT_ACCESS_TSS;
    tssd->limit_high_flags= 0x00;
    tssd->base_high       = (tss_base >> 24) & 0xFF;
    tssd->base_upper      = (tss_base >> 32) & 0xFFFFFFFF;
    tssd->reserved        = 0;

    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base  = (u64)&gdt_table;

    gdt_load(&gdtr);
    /* TSS selector: 6 * 8 = 0x30 */
    tss_flush(0x30);
}

void gdt_set_rsp0(u64 rsp0) {
    tss.rsp[0] = rsp0;
}

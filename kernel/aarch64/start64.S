/* KengaOS — точка входа aarch64 (Limine v12 protocol, UEFI, base revision 6).
 *
 * Limine v12 на входе уже настроил: EL1 (или EL2+VHE), MMU+HHDM, стек не
 * гарантирован. Ответы Limine записаны в .limine_requests (те же
 * magic-маркеры, что на x86_64).
 *
 * Что делает вход:
 *   1. EL2 -> EL1 drop (если завели на EL2)
 *   2. стек, VBAR_EL1, FPEN (rev 6: CPACR_EL1=0 на входе)
 *   3. identity-map 0x0..0x3FFFFFFF как Device-nGnRE через свободный
 *      TTBR0_EL1 (rev 6 мапит только RAM/framebuffer — UART и GIC нет):
 *      PL011 0x09000000, GICv2 0x08000000/0x08010000, PCI 0x10000000
 *   4. bl k_arch_uart_init -> bl main (kmain.c, один и тот же для x86/a64)
 *
 * PA = VA - virtual_base + physical_base — из executable-address response
 * (hhdm-оффсет не подходит: образ ядра живёт на своей higher-half базе).
 */

/* "aw": Limine должен иметь возможность записывать response-указатели в запросы. */
.section .limine_requests, "aw"
.align 8

.global limine_requests_start_marker
limine_requests_start_marker:
    .quad 0xf6b8f4b39de7d1ae
    .quad 0xfab91a6940fcb9cf
    .quad 0x785c6ed015d3e316
    .quad 0x181e920a7852b9d9

.global limine_base_revision
limine_base_revision:
    .quad 0xf9562b2d5c95a6c8
    .quad 0x6a7b384944536bdc
    .quad 6   /* aarch64: Limine 12 требует base revision >= 6 */

/* struct limine_bootloader_info_request:
     id[4] | revision (u64) | response* (u64)  — response на offset 40 */
.global limine_bootloader_info_request
limine_bootloader_info_request:
    .quad 0xc7b1dd30df4c8b88
    .quad 0x0a82e883a194f07b
    .quad 0xf55038d8e2a1202f
    .quad 0x279426fcf5f59740
    .quad 0
    .quad 0

/* struct limine_framebuffer_request */
.global limine_framebuffer_request
limine_framebuffer_request:
    .quad 0xc7b1dd30df4c8b88   /* LIMINE_COMMON_MAGIC hi */
    .quad 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC lo */
    .quad 0x9d5827dcd881dd75   /* framebuffer request id */
    .quad 0xa3148604f6fab11b
    .quad 0
    .quad 0

/* struct limine_hhdm_request */
.global limine_hhdm_request
limine_hhdm_request:
    .quad 0xc7b1dd30df4c8b88   /* LIMINE_COMMON_MAGIC hi */
    .quad 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC lo */
    .quad 0x48dcf1cb8ad2b852   /* hhdm request id */
    .quad 0x63984e959a98244b
    .quad 0
    .quad 0

/* struct limine_module_request */
.global limine_module_request
limine_module_request:
    .quad 0xc7b1dd30df4c8b88   /* LIMINE_COMMON_MAGIC hi */
    .quad 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC lo */
    .quad 0x3e7e279702be32af   /* module request id */
    .quad 0xca1c4f3bd1280cee
    .quad 0
    .quad 0

/* struct limine_memmap_request */
.global limine_memmap_request
limine_memmap_request:
    .quad 0xc7b1dd30df4c8b88   /* LIMINE_COMMON_MAGIC hi */
    .quad 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC lo */
    .quad 0x67cf3d9d378a806f   /* memmap request id */
    .quad 0xe304acdfc50c3c62
    .quad 0
    .quad 0

/* struct limine_executable_address_request — база образа для PA-расчёта */
.global limine_exec_address_request
limine_exec_address_request:
    .quad 0xc7b1dd30df4c8b88   /* LIMINE_COMMON_MAGIC hi */
    .quad 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC lo */
    .quad 0x71ba76863cc55f63   /* executable address request id */
    .quad 0xb2644a48c516a487
    .quad 0
    .quad 0

.global limine_requests_end_marker
limine_requests_end_marker:
    .quad 0xadc0e0531bb10d03
    .quad 0x9572709f31764c62

.section .text._start, "ax"

.global _start
_start:
    /* Base revision 6: вход на EL1 или EL2(+VHE). Весь арх-слой написан для
       EL1 (VBAR_EL1, DAIF, CNTV) — если завели на EL2, падаем на EL1.
       На cortex-a72 VHE нет, Limine сам дал бы EL1, но drop не повредит. */
    mrs     x29, CurrentEL
    cmp     x29, #0x8
    b.ne    .Lat_el1

    /* --- EL2 -> EL1 --- */
    mov     x29, #0
    msr     hcr_el2, x29           /* без трэпов виртуализации */
    mov     x29, #3
    msr     cnthctl_el2, x29       /* EL1PCTEN|EL1PCEN */
    msr     cntvoff_el2, xzr
    ldr     x29, =.Lat_el1
    msr     elr_el2, x29
    mov     x29, #0xF5             /* DAIF маскированы, M = EL1h */
    msr     spsr_el2, x29
    eret

.Lat_el1:
    /* SP: гарантируем SPx (SP_EL1), даже если вошли с M=EL1t. */
    msr     spsel, #1

    /* Стек и вектора — как можно раньше: фолт до VBAR ловится молча. */
    ldr     x29, =stack_top
    mov     sp, x29
    ldr     x29, =k_a64_vectors
    msr     vbar_el1, x29
    isb

    /* PA = VA_hhdm - hhdm_offset. Таблицы кладём в RAM по фиксированному
       PA 0x50000000 (пишем через HHDM-вид — он замаплен на входе), поэтому
       база образа ядра вообще не нужна. ponytail: 0x50000000 выбрана
       "в середине RAM" для QEMU virt -m 512M; при переносе на железо
       брать верхнюю страницу крупнейшей usable-области из memmap. */
    ldr     x20, =limine_hhdm_request + 40
    ldr     x20, [x20]             /* response* */
    cbz     x20, .Lno_exec_addr
    ldr     x21, [x20, #8]         /* hhdm offset (revision @0, offset @8) */
    cbz     x21, .Lno_exec_addr

    /* MMIO-окно: L0[0] -> L1[0] -> 1GiB block, identity 0x0..0x3FFFFFFF,
       Device-nGnRE (Attr2). T0SZ=16, гранула 4K -> обход с уровня 0. */
    ldr     x22, =0x50000000       /* PA таблиц (L0), L1 — на +0x1000 */
    add     x23, x21, x22          /* VA(L0) через HHDM */
    add     x24, x23, #0x1000      /* VA(L1) */
    orr     x25, x22, #0x3         /* table descriptor: PA(L0)|valid|type */
    add     x25, x25, #0x1000
    str     x25, [x23]             /* l0[0] -> l1 (PA 0x50001000) */
    mov     x25, #1                /* valid, bit1=0 -> block (1 GiB) */
    orr     x25, x25, #(1 << 10)   /* AF */
    orr     x25, x25, #(2 << 2)    /* AttrIndx = 2 (MAIR Attr2) */
    str     x25, [x24]             /* l1[0] = 1GiB Device block */

    /* Высокое PCI-окно QEMU virt: 64-bit BAR'ы (напр. xHCI) EDK2 кладёт с
       0x8000000000. L0[1] -> L1b (PA 0x50002000), L1b[0] = 1GiB Device block
       c PA 0x8000000000 (OA = 0x200). */
    add     x20, x22, #0x2000      /* PA(L1b) */
    add     x24, x21, x20          /* VA(L1b) через HHDM */
    orr     x25, x20, #0x3
    str     x25, [x23, #8]         /* l0[1] -> l1b */
    mov     x25, #1
    orr     x25, x25, #(1 << 10)
    orr     x25, x25, #(2 << 2)
    mov     x26, #0x200
    lsl     x26, x26, #30          /* 0x8000000000 */
    orr     x25, x25, x26
    str     x25, [x24]             /* l1b[0] = 1GiB Device block */

    msr     ttbr0_el1, x22
    mrs     x28, mair_el1
    mov     x27, #0x04             /* Device-nGnRE */
    bfi     x28, x27, #16, #8      /* Attr2 */
    msr     mair_el1, x28
    dsb     sy
    isb

    /* FP/NEON: rev 6 гарантирует CPACR_EL1 = 0; clang -O2 вправе
       векторизовать (NEON) — включаем FPEN=3, иначе любой fmov -> trap. */
    mrs     x29, cpacr_el1
    orr     x29, x29, #(3 << 20)
    msr     cpacr_el1, x29
    isb

    bl      k_arch_uart_init       /* PL011: не зависим от serial: yes в limine.conf */
    bl      main

.halt:
    wfi
    b       .halt

.Lno_exec_addr:                    /* нет ответа executable-address — не стартуем */
    bl      k_arch_uart_init
    ldr     x0, =.Lno_addr_msg
    bl      k_arch_uart_puts
    b       .halt

.section .rodata.str, "a"
.Lno_addr_msg:
    .ascii "\r\n[KengaOS] no executable-address response, halting\r\n"

.section .bss.stack, "aw", @nobits
.balign 16
stack_bottom:
    .skip 16384
stack_top:

/* TTBR0: L0 (512x8B) + L1 (512x8B), обход с уровня 0 (T0SZ=16, гранула 4K).
   l0[0] -> l1, l1[0] = 1GiB Device block (см. .Lat_el1). */
.section .bss.pgtable, "aw", @nobits
.balign 4096
.global ttbr0_l0
ttbr0_l0:
    .skip 4096
.global ttbr0_l1
ttbr0_l1:
    .skip 4096

/* Kenga FFI: kf_get_boot_info() -> i64
   Возвращает limine_bootloader_info_request.response
   (revision u64 @0, name* @8, version* @16). */
.section .text._start, "ax"
.global k_kf_get_boot_info
.type k_kf_get_boot_info, @function
k_kf_get_boot_info:
    ldr     x0, =limine_bootloader_info_request + 40
    ldr     x0, [x0]
    ret

/* Kenga FFI: kf_get_framebuffer() -> i64
   Первый (нулевой) framebuffer — struct limine_framebuffer*, или 0.
   Response: revision u64 @0, framebuffer_count u64 @8, framebuffers** @16. */
.global k_kf_get_framebuffer
.type k_kf_get_framebuffer, @function
k_kf_get_framebuffer:
    ldr     x0, =limine_framebuffer_request + 40
    ldr     x0, [x0]
    cbz     x0, 1f
    ldr     x0, [x0, #16]       /* framebuffers** */
    cbz     x0, 1f
    ldr     x0, [x0]            /* framebuffers[0] */
1:  ret

/* Kenga FFI: kf_get_hhdm() -> i64 — HHDM-оффсет (u64 @8 в response), или 0. */
.global k_kf_get_hhdm
.type k_kf_get_hhdm, @function
k_kf_get_hhdm:
    ldr     x0, =limine_hhdm_request + 40
    ldr     x0, [x0]
    cbz     x0, 1f
    ldr     x0, [x0, #8]        /* response->offset */
1:  ret

/* Kenga FFI: kf_get_memmap() -> i64
   (revision u64 @0, entries u64 @8, entries** u64 @16). */
.global k_kf_get_memmap
.type k_kf_get_memmap, @function
k_kf_get_memmap:
    ldr     x0, =limine_memmap_request + 40
    ldr     x0, [x0]
    ret

/* Kenga FFI: kf_get_modules() -> i64
   (revision u64 @0, module_count u64 @8, modules** u64 @16). */
.global k_kf_get_modules
.type k_kf_get_modules, @function
k_kf_get_modules:
    ldr     x0, =limine_module_request + 40
    ldr     x0, [x0]
    ret

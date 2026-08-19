/* KengaOS — точка входа x86_64 (Limine v12 protocol).

   Limine v12 на входе уже настроил:
     - long mode + paging (HHDM)
     - стек
   Запросы (bootloader_info) объявлены в секции .limine_requests —
   Limine сам находит их по magic-маркерам и заполняет response-указатели.
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
    .quad 0

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

/* struct limine_framebuffer_request:
     id[4] | revision (u64) | response* (u64)  — response на offset 40 */
.global limine_framebuffer_request
limine_framebuffer_request:
    .quad 0xc7b1dd30df4c8b88   /* LIMINE_COMMON_MAGIC hi */
    .quad 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC lo */
    .quad 0x9d5827dcd881dd75   /* framebuffer request id */
    .quad 0xa3148604f6fab11b
    .quad 0
    .quad 0

/* struct limine_hhdm_request: id[4] | revision (u64) | response* (u64) */
.global limine_hhdm_request
limine_hhdm_request:
    .quad 0xc7b1dd30df4c8b88   /* LIMINE_COMMON_MAGIC hi */
    .quad 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC lo */
    .quad 0x48dcf1cb8ad2b852   /* hhdm request id */
    .quad 0x63984e959a98244b
    .quad 0
    .quad 0

/* struct limine_memmap_request: id[4] | revision (u64) | response* (u64) */
/* struct limine_module_request: id[4] | revision (u64) | response* (u64) */
.global limine_module_request
limine_module_request:
    .quad 0xc7b1dd30df4c8b88   /* LIMINE_COMMON_MAGIC hi */
    .quad 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC lo */
    .quad 0x3e7e279702be32af   /* module request id */
    .quad 0xca1c4f3bd1280cee
    .quad 0
    .quad 0

.global limine_memmap_request
limine_memmap_request:
    .quad 0xc7b1dd30df4c8b88   /* LIMINE_COMMON_MAGIC hi */
    .quad 0x0a82e883a194f07b   /* LIMINE_COMMON_MAGIC lo */
    .quad 0x67cf3d9d378a806f   /* memmap request id */
    .quad 0xe304acdfc50c3c62
    .quad 0
    .quad 0

.global limine_requests_end_marker
limine_requests_end_marker:
    .quad 0xadc0e0531bb10d03
    .quad 0x9572709f31764c62

.section .text._start, "ax"
.code64
.intel_syntax noprefix

.global _start
_start:
    lea rsp, [rip + stack_top]
    and rsp, ~0xF
    call main

.halt:
    hlt
    jmp .halt

/* Kenga FFI: kf_get_boot_info() -> i64
   Возвращает limine_bootloader_info_request.response
   (struct limine_bootloader_info_response*:
      revision u64 @0, name* @8, version* @16). */
.global k_kf_get_boot_info
.type k_kf_get_boot_info, @function
k_kf_get_boot_info:
    mov rax, [rip + limine_bootloader_info_request + 40]
    ret

/* Kenga FFI: kf_get_framebuffer() -> i64
   Возвращает первый (нулевой) framebuffer — struct limine_framebuffer*,
   или 0, если framebuffer не выдан.
   Response: revision u64 @0, framebuffer_count u64 @8, framebuffers** @16. */
.global k_kf_get_framebuffer
.type k_kf_get_framebuffer, @function
k_kf_get_framebuffer:
    mov rax, [rip + limine_framebuffer_request + 40]
    test rax, rax
    jz .kfb_done
    mov rax, [rax + 16]          /* framebuffers** */
    test rax, rax
    jz .kfb_done
    mov rax, [rax]               /* framebuffers[0] */
.kfb_done:
    ret

/* Kenga FFI: kf_get_hhdm() -> i64
   Возвращает HHDM-оффсет (u64 @0 в response), или 0, если не выдан.
   К физическому адресу фреймбуфера нужно прибавить его, чтобы получить
   доступный в higher-half виртуальный адрес. */
.global k_kf_get_hhdm
.type k_kf_get_hhdm, @function
k_kf_get_hhdm:
    mov rax, [rip + limine_hhdm_request + 40]
    test rax, rax
    jz .khhdm_done
    mov rax, [rax]               /* response->offset */
.khhdm_done:
    ret

/* Kenga FFI: kf_get_memmap() -> i64
   Returns limine_memmap_request.response
   (struct limine_memmap_response*:
      revision u64 @0, entries u64 @8, entries** u64 @16). */
.global k_kf_get_memmap
.type k_kf_get_memmap, @function
k_kf_get_memmap:
    mov rax, [rip + limine_memmap_request + 40]
    ret

/* Kenga FFI: kf_get_modules() -> i64
   Returns limine_module_request.response
   (struct limine_module_response*:
      revision u64 @0, module_count u64 @8, modules** u64 @16). */
.global k_kf_get_modules
.type k_kf_get_modules, @function
k_kf_get_modules:
    mov rax, [rip + limine_module_request + 40]
    ret

.section .bss.stack, "aw", @nobits
.align 16
stack_bottom:
    .skip 16384
stack_top:
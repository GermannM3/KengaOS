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

.section .bss.stack, "aw", @nobits
.align 16
stack_bottom:
    .skip 16384
stack_top:
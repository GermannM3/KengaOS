/*  KengaOS — точка входа x86_64 (Limine v12 protocol).
    GAS/AT&T синтаксис.

    Limine v12 уже настроил:
      - long mode
      - paging с HHDM (Higher Half Direct Map)
      - стек
      - передачу boot_info в rdi (опционально)

    Кладём BSS, выравниваем стек, вызываем kmain.
*/
.section .text
.global _kentry
.extern kmain
.extern __bss_start
.extern __bss_end

_kentry:
    /* 0. Очистить BSS */
    movabsq $__bss_start, %rdi
    movabsq $__bss_end,   %rcx
    subq    %rdi, %rcx
    xorq    %rax, %rax
    cld
    rep stosb

    /* 1. rdi уже содержит указатель boot info от Limine (если передан) */

    /* 2. Выровнять стек на 16 байт */
    andq    $-16, %rsp

    /* 3. Вызвать kmain() */
    call    kmain

    /* 4. Если вернулась — зависнуть */
.hang:
    cli
    hlt
    jmp     .hang

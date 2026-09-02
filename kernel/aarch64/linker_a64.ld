/* KengaOS — linker script для aarch64 (Limine v12 protocol, UEFI).
   Limine требует на aarch64 ту же higher-half базу, что и на x86_64
   (FIXED_HIGHER_HALF_OFFSET_64 = 0xffffffff80000000, см. common/lib/elf.h).
   Секция .limine_requests — первая, в отдельном PT_LOAD (Limine ищет
   magic-маркеры по ней). Все секции выровнены по 4K: PT_LOAD-сегменты
   с разными правами не должны делить страницу.
*/

OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS
{
    limine_requests PT_LOAD;
    text            PT_LOAD FLAGS(5);   /* R-X */
    rodata          PT_LOAD FLAGS(4);   /* R   */
    data            PT_LOAD FLAGS(6);   /* RW  */
}

SECTIONS
{
    . = 0xFFFFFFFF80000000;

    .limine_requests : {
        KEEP(*(.limine_requests))
    } : limine_requests

    . = ALIGN(4K);
    .text : {
        *(.text .text.*)
    } : text

    . = ALIGN(4K);
    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } : rodata

    . = ALIGN(4K);
    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.got .got.*)
    } : data

    . = ALIGN(4K);
    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
    } : data

    /DISCARD/ : {
        *(.eh_frame .eh_frame_hdr .note .note.* .comment)
    }
}

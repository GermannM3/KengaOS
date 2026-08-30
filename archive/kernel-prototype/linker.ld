/* KengaOS — linker script для x86_64 (Limine v12 protocol).
   Высокоуровневое ядро: загружается по адресу 0xFFFFFFFF80000000.
   Секция .limine_requests — первая, в отдельном PT_LOAD (так ищет Limine).
   Все секции выровнены по 4K: PHDR-сегменты с разными правами НЕ должны
   разделять одну страницу (иначе Limine: "PHDRs with different permissions
   sharing the same memory page").
*/

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    limine_requests PT_LOAD;
    text            PT_LOAD;
    rodata          PT_LOAD;
    data            PT_LOAD;
}

SECTIONS
{
    . = 0xFFFFFFFF80000000;

    .limine_requests : {
        KEEP(*(.limine_requests))
    } : limine_requests

    . = ALIGN(4K);
    .text : {
        *(.text._start)
        *(.text .text.*)
        *(.ltext .ltext.*)
    } : text

    . = ALIGN(4K);
    .rodata : {
        *(.rodata .rodata.*)
        *(.lrodata .lrodata.*)
    } : rodata

    . = ALIGN(4K);
    .data : {
        *(.data .data.*)
    } : data

    . = ALIGN(4K);
    .bss : {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    } : data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
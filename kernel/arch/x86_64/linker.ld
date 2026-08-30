/*  KengaOS — linker script для x86_64 (Limine v12 protocol)
    Высокоуровневое ядро: загружается по адресу 0xFFFFFFFF80000000.
*/
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_kentry)

PROVIDE(__kernel_start = 0xFFFFFFFF80000000);
PROVIDE(__kernel_end = .);

PHDRS
{
    limine_requests PT_LOAD;
    text            PT_LOAD;
    rodata          PT_LOAD;
    data            PT_LOAD;
}

SECTIONS
{
    /* Лимин кладёт запросы в начало образа */
    . = 0xFFFFFFFF80000000;

    /* Каждая PT_LOAD секция выровнена по странице, иначе Limine
       паникует ("PHDRs with different permissions sharing the same
       memory page"), когда две секции с разными правами лежат на
       одной странице. */
    . = ALIGN(0x1000);
    .limine_requests : {
        KEEP(*(.limine_requests))
    } : limine_requests

    . = ALIGN(0x1000);
    .text : {
        *(.text .text.*)
    } : text

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata .rodata.*)
    } : rodata

    . = ALIGN(0x1000);
    .data : {
        *(.data .data.*)
    } : data

    . = ALIGN(0x1000);
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } : data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}

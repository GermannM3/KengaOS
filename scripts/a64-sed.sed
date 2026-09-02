# a64-sed.sed — переписывание x86-asm в C под aarch64-хуки.
#
# ponytail: правильное место для этого — флаг --arch в kenga-lang
# (codegen.rs, интринсики asm_inb/outb/hlt/cli/sti). Пока компилятор
# пинаем снаружи: паттерны эмиссии детерминированы (см. codegen.rs:1003+),
# компилятор pinned, при апгрейде kenga-lang этот файл сверяется первым.

# Кодогенератор (kmain.c): asm_outb -> k_arch_io_outb(порт, значение)
s/__asm__ __volatile__("outb %0, %1" : : "a"\(.*\), "Nd"\(.*\));/k_arch_io_outb(\2, \1);/
# Кодогенератор: asm_inb -> k_arch_io_inb(порт)
s/__asm__ __volatile__("inb %1, %0" : "=a"(_k_port_in) : "Nd"\(.*\));/_k_port_in = k_arch_io_inb(\1);/

# Runtime (k_die в сгенерированном C) и desktop.kenga: hlt
s/__asm__ __volatile__("hlt");/k_arch_hlt();/
# Паника runtime: cli;hlt
s/__asm__ __volatile__("cli; hlt");/k_arch_halt_forever();/
# kf_shell.c: sti перед idle-циклом
s/__asm__ __volatile__("sti");/k_arch_irq_enable();/
s/__asm__ __volatile__("cli");/k_arch_irq_disable();/

# Прямые записи в COM1 из kf_fb/kf_proc/kf_shell (отладочный вывод) —
# уже покрыты общим outb-паттерном выше (порт 0x3F8).

# RUNTIME_FS кодогенератора включает <math.h>, но libm-вызовов в
# freestanding-рантайме нет — при -nostdinc заголовка не существует.
/#include <math.h>/d

# Kenga-мост KengaOS ↔ kenga-lang

Этот файл описывает, какие расширения нужны в `lower_c` (генераторе C
из Kenga в kenga-lang), чтобы KengaOS могла начать миграцию C → Kenga.

См. также: https://github.com/GermannM3/kenga-lang/blob/main/docs/REPLACE_RUST.md

---

## M0: Freestanding-режим для `lower_c`

### Что нужно

Флаг `--freestanding` в CLI `kenga emit-c`. Когда он включён:

1. Не эмитятся `#include <stdio.h>`, `<stdlib.h>`, `<string.h>`.
2. Эмитятся только `#include <stdint.h>` (для `uint8_t`/`uint64_t`/...).
3. `memcpy`/`memset`/`memcmp` инлайнятся (через `__builtin_memcpy` или
   ручные циклы).
4. `printf`/`println` не эмитятся (или заменяются на `_kenga_print_stub`,
   который ядро определяет само).

### Почему

Сейчас `lower_c` генерирует C, который линкуется с libc. Ядро не может
линковаться с libc — у него нет процесса, нет файловой системы, нет malloc.

### Приоритет: критично для v0.1.0

---

## M1: Raw pointers / MMIO

### Что нужно

Встроенные функции (не библиотечные, а компиляторные):

```kenga
fn mmio_read<T>(addr: u64) -> T;
fn mmio_write<T>(addr: u64, val: T) -> ();
```

Компилируются в:
```c
*(volatile T*)addr    // для read
*(volatile T*)addr = val    // для write
```

### Почему

Драйверы железа (UART, framebuffer, PCI config space) должны писать
по конкретным физическим адресам. Без volatile компилятор схлопнет
записи.

### Приоритет: критично для v0.2.0 (драйверы)

---

## M2: Inline assembly

### Что нужно

```kenga
asm!("x86_64", "cli");
asm!("x86_64", "outb %0, %1", in "a"(val), in "Nd"(port));
```

Минимальный синтаксис: один строковый литерал (архитектура) + один
строковый литерал (ассемблерный код) + optional список операндов
(`in`/`out` с регистрами).

### Почему

GDT/IDT load (`lgdt`, `lidt`), interrupt enable/disable (`cli`/`sti`),
context switch (`iretq`), IO port access — без этого ядро не построить.

### Приоритет: критично для v0.3.0 (kmain.kenga)

---

## M3: Атомарные операции

### Что нужно

```kenga
fn atomic_load<T>(addr: *T) -> T;
fn atomic_store<T>(addr: *T, val: T) -> ();
fn atomic_cas<T>(addr: *T, expected: *T, desired: T) -> bool;
fn atomic_fence() -> ();
```

Компилируются в `__atomic_load`/`__atomic_store`/`__atomic_compare_exchange`
GCC builtins.

### Почему

Когда появится планировщик и SMP — без атомариков не сделать spinlock'и
и lock-free очереди.

### Приоритет: нужен для v0.4.0 (агентная модель)

---

## M4: FFI к C

### Что нужно

```kenga
extern "C" {
    fn kprintf(fmt: *u8, ...) -> i32;
}
```

Компилируется в:
```c
extern int kprintf(const char* fmt, ...);
```

### Почему

На этапе миграции часть кода будет на C, часть на Kenga. Kenga-модули
должны звать C-функции ядра.

### Приоритет: нужен для v0.1.1

---

## M5: Bit-packing и structs с выравниванием

### Что нужно

- Поддержка `#[repr(C)]` (или эквивалента) — структуру как в C.
- `#[repr(packed)]` — без padding.
- Управление выравниванием полей.

### Почему

Структуры Limine, descriptor tables, заголовки ФС — имеют
специфицированное C-представление. Kenga-код должен уметь их читать
точно так же.

### Приоритет: нужен для v0.2.0

---

## M6: Union типы

### Что нужно

```kenga
union KVal {
    Int: i64,
    Str: *u8,
    List: *KVal,
}
```

### Почему

`lower_kv` в kenga-lang уже работает с тегированным KVal. Для ядра
это естественный способ представления гетерогенных данных (например,
аргументы команд или свойства агентов).

### Приоритет: нужен для v0.4.0

---

## Резюме

| Milestone в kenga-lang | Что открывает в KengaOS | KengaOS-версия |
|------------------------|-------------------------|----------------|
| M0: freestanding       | libc.kenga              | v0.1.0         |
| M1: mmio_*             | drivers/*.kenga         | v0.2.0         |
| M2: asm!               | kernel/kmain.kenga      | v0.3.0         |
| M3: atomic_*           | spinlocks, SMP          | v0.4.0         |
| M4: FFI                | постепенная миграция    | v0.1.1         |
| M5: repr(C)            | struct-совместимость    | v0.2.0         |
| M6: union              | KVal в ядре             | v0.4.0         |

Без M0–M2 KengaOS остаётся C-ядром. Это не катастрофа (большинство ОС
на C), но теряется смысл «Kenga-нативной ОС».

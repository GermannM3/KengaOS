# KengaOS — lang/ директория

В этой директории лежат **.kenga-файлы** — эталонные реализации модулей ядра,
написанные на Kenga. Они **не используются** текущим ядром (v0.0.x собирается
на C), но служат двум целям:

1. **Задел на будущее.** Когда `lower_c` в [kenga-lang](https://github.com/GermannM3/kenga-lang)
   получит необходимые расширения (см. [`docs/KENGA_BRIDGE.md`](../docs/KENGA_BRIDGE.md)),
   эти файлы можно будет компилировать в freestanding C99 и линковать с ядром.

2. **Документация намерений.** Каждый .kenga-файл зеркалит соответствующий
   .c-файл в `kernel/`. Читать их параллельно — видно, как именно будет
   выглядеть миграция.

---

## Структура

```
lang/
├── libc/
│   └── libc.kenga              # kstrlen / kstrcmp / kmemset / kmemcpy
│                                (мигрирует из kernel/lib/libc.c)
├── i18n/
│   └── i18n.kenga              # интернационализация
│                                (мигрирует из kernel/i18n/i18n.c)
├── drivers/
│   └── fb.kenga                # framebuffer + UTF-8 decoder
│                                (мигрирует из kernel/drivers/fb.c)
└── kernel/
    ├── buddy.kenga             # buddy page allocator
    │                            (мигрирует из kernel/mem/buddy.c)
    └── kmain.kenga             # точка входа
                                 (мигрирует из kernel/kmain.c)
```

---

## Зависимости от расширений kenga-lang

| Файл | Что нужно | M*  |
|------|-----------|-----|
| `libc/libc.kenga`     | `--freestanding`           | M0 |
| `i18n/i18n.kenga`     | `--freestanding`, `repr(C)` | M0, M5 |
| `drivers/fb.kenga`    | `--freestanding`, `mmio_*`  | M0, M1 |
| `kernel/buddy.kenga`  | `--freestanding`, `repr(C)` | M0, M5 |
| `kernel/kmain.kenga`  | `--freestanding`, `mmio_*`, `asm!` | M0, M1, M2 |

См. [`docs/KENGA_BRIDGE.md`](../docs/KENGA_BRIDGE.md) — полный список
расширений с приоритетами.

---

## Запуск в lite-режиме (сегодня)

Некоторые файлы (`libc.kenga`, `i18n.kenga`, `buddy.kenga`) можно запустить
в `kenga-lite` прямо сейчас, как обычные Kenga-программы:

```bash
kenga run --lite lang/libc/libc.kenga
kenga run --lite lang/i18n/i18n.kenga
kenga run --lite lang/kernel/buddy.kenga
```

Файлы `drivers/fb.kenga` и `kernel/kmain.kenga` **не запустятся** — они
требуют mmio/asm, которых пока нет в lite-режиме. Но их можно читать как
документацию того, как будет выглядеть ядро после миграции.

---

## Соответствие C ↔ Kenga

| C-файл в `kernel/` | .kenga в `lang/` | Когда линкуется |
|--------------------|------------------|-----------------|
| `lib/libc.c`       | `libc/libc.kenga`     | v0.1.1 |
| `i18n/i18n.c`      | `i18n/i18n.kenga`     | v0.1.2 |
| `drivers/fb.c`     | `drivers/fb.kenga`    | v0.2.0 |
| `mem/buddy.c`      | `kernel/buddy.kenga`  | v0.3.1 |
| `kmain.c`          | `kernel/kmain.kenga`  | v0.3.0 |

Подробности — в [`docs/ROADMAP.md`](../docs/ROADMAP.md).

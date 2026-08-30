# KengaOS

**Операционная система, написанная на языке Kenga.**

KengaOS — проект по созданию полноценной ОС, ядро которой в конечном счёте
будет написано на языке программирования Kenga (https://github.com/GermannM3/kenga-lang),
а не на C. Текущая версия (0.0.5 «Заря») использует C-стартовую заглушку,
которая будет постепенно замещаться на pure-Kenga код по мере роста `lower_c`
(генератора C из Kenga).

---

## Что это

KengaOS v0.0.5 — пятая загружаемая версия. Она:

- ✅ Грузится на голом железе и в QEMU через Limine bootloader
- ✅ Выводит на экран текст на русском и английском (UTF-8)
- ✅ Поддерживает переключение языков в runtime (`lang ru` / `lang en`)
- ✅ Принимает ввод с клавиатуры PS/2 (раскладка US QWERTY)
- ✅ Имеет shell с командами `help`, `info`, `lang`, `reboot`, `threads`, `mem`, `spawn`, `ls`, `cat`, `layout`, `exec`
- ✅ **Buddy page allocator** для управления физической памятью
- ✅ **Round-robin планировщик потоков** с context switch на asm
- ✅ **Демо-потоки**: команда `spawn` создаёт 2 потока, `threads` показывает статус
- ✅ Пишет отладочный вывод в UART (COM1, 115200 бод)
- ✅ Содержит **.kenga-модули** для будущей миграции (`lang/`)
- ✅ **VFS с initrd** (ustar tar архив, загружается Limine)
- ✅ **Русская раскладка клавиатуры** (ЙЦУКЕН, переключение через `layout` или F12)
- ✅ **Spinlock и Semaphore** для синхронизации потоков
- ✅ **Limine v12 protocol** (актуальная версия)
- ✅ **User mode (ring 3)** — запуск ELF-программ с изоляцией памяти
- ✅ **VMM (пейджинг)** — 4-level paging, отдельные address space для процессов
- ✅ **Syscall interface** (SYSCALL/SYSRET) — exit, write, yield, get_pid
- ✅ **ELF loader** — загружает static ELF64 из initrd
- ✅ **Тестовая user-программа** hello.elf (пишет в stdout, получает PID)

Полноценный roadmap — в [`docs/ROADMAP.md`](docs/ROADMAP.md).

---

## Быстрый старт

### Требования

- Linux (Ubuntu/Debian/Arch/WSL2)
- `gcc`, `ld`, `make`, `nasm`, `clang`, `lld`, `mtools`, `xorriso`, `qemu-system-x86_64`

На Debian/Ubuntu/WSL2:
```bash
sudo apt install build-essential nasm clang lld mtools xorriso qemu-system-x86
```

### Сборка и запуск

```bash
git clone <repo-url>
cd kenga-os

# Сборка ядра (быстро)
make

# Сборка ISO (скачает и соберёт Limine при первой сборке)
make iso

# Запуск в QEMU
make run-iso
```

Первая сборка ISO сама скачает Limine v12.6.0 и соберёт его (нужны clang+lld+nasm).

### Что вы увидите

На экране framebuffer появится ASCII-баннер «KENGA OS», затем — загрузочные
сообщения на русском, и приглашение оболочки:

```
kenga>
```

Введите `help` — увидите список команд. Введите `info` — информацию о системе.
Введите `lang en` — переключите интерфейс на английский.
Введите `layout` — переключите раскладку клавиатуры (или F12 в runtime).
Введите `ls` — список файлов в initrd, `cat README.txt` — прочитать файл.
Введите `spawn` — запустить 2 демо-потока, `threads` — статус.

В UART-логе (serial) — те же сообщения, плюс ранние отладочные строки
до инициализации framebuffer.

### Запуск на Windows через WSL2

Если у вас Windows, easiest путь — WSL2:

```powershell
# В PowerShell от админа
wsl --install
# Перезагрузка → в Ubuntu
sudo apt update
sudo apt install build-essential nasm clang lld mtools xorriso qemu-system-x86
```

После этого распакуйте архив в WSL и соберите как обычно.

Для запуска QEMU с графическим окном — нужен X-сервер (VcXsrv или WSLg,
который уже встроен в Windows 11). Если графику не настраивать, можно
запускать `make run-iso-nographic` — только UART вывод.

---

## Структура проекта

```
kenga-os/
├── kernel/
│   ├── kmain.c                    # Точка входа на C (временная)
│   ├── arch/x86_64/
│   │   ├── entry.S                # Ассемблерная точка входа
│   │   ├── isr_asm.S              # ISR-обёртки (сохранение регистров)
│   │   ├── switch.S               # Context switch между потоками
│   │   ├── linker.ld              # Linker script (higher-half)
│   │   ├── limine.h               # Limine boot protocol
│   │   ├── limine_requests.c      # Запросы к загрузчику
│   │   ├── gdt.c / gdt.h          # GDT + TSS
│   │   ├── idt.c / idt.h          # IDT + PIC 8259
│   │   └── io.h                   # inb/outb/inl/outl
│   ├── drivers/
│   │   ├── uart.h                 # UART 16550 (header-only)
│   │   ├── fb.c / fb.h            # Framebuffer + текстовая консоль
│   │   ├── pit.c / pit.h          # PIT 8254 (1000 Гц таймер)
│   │   └── kbd.c / kbd.h          # PS/2 клавиатура
│   ├── mem/
│   │   └── buddy.c / buddy.h      # Buddy page allocator
│   ├── sched/
│   │   ├── thread.c / thread.h    # Структура и создание потоков
│   │   └── scheduler.c / scheduler.h  # Round-robin планировщик
│   ├── i18n/
│   │   └── i18n.c / i18n.h        # i18n (RU/EN, runtime switch)
│   └── lib/
│       ├── types.h                # u8/u16/u32/u64/...
│       └── libc.c / libc.h        # kprintf, kstrlen, kstrcmp, ...
├── fonts/
│   └── font.c / font.h            # Шрифт 8x16 с кириллицей
├── lang/                          # .kenga-модули для будущей миграции
│   ├── README.md
│   ├── libc/libc.kenga
│   ├── i18n/i18n.kenga
│   ├── drivers/fb.kenga
│   └── kernel/{buddy,kmain}.kenga
├── limine.conf                    # Конфигурация загрузчика
├── Makefile
├── scripts/
│   ├── fetch-limine.sh
│   └── build-and-run.sh
└── docs/
    ├── ARCHITECTURE.md
    ├── ROADMAP.md
    └── KENGA_BRIDGE.md
```

---

## Дизайн интерфейса и языков

**Русский — язык интерфейса по умолчанию.** Это сознательное решение:
оригинальный автор Kenga пишет по-русски, и сообщество вокруг языка —
русскоязычное. KengaOS не должна быть «сначала английской, потом переведённой».

**Переключение языков** — как у взрослых ОС (Windows, macOS, GNOME):
- В коде используются не строки, а **идентификаторы** (`STR_BOOT_STARTING`, …)
- Runtime: lookup по текущему языку → UTF-8 строка
- Переключение мгновенное, не требует перезагрузки
- Добавить новый язык = дописать один столбец в `i18n.c`

См. [`kernel/i18n/i18n.h`](kernel/i18n/i18n.h) — полный список идентификаторов.
См. [`kernel/i18n/i18n.c`](kernel/i18n/i18n.c) — таблицы RU и EN.

---

## Связь с Kenga-lang

Эта ОС — **не отдельный проект**, а целевая область применения языка Kenga.
Текущая стратегия миграции (см. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)):

1. **v0.0.x** — C-ядро. Kenga используется для написания тестов и утилит.
2. **v0.1.x** — Часть драйверов переписана на Kenga, откомпилирована через
   `lower_c` (когда у него появится `--freestanding` режим).
3. **v0.2.x** — Kenga-оболочки (shell, init). C-ядро как «бутстрап».
4. **v0.3.x** — Само ядро полностью на Kenga. C остаётся только как fallback
   для критичных участков (обработчики прерываний, GDT/IDT).
5. **v1.0** — Полный self-host: Kenga-компилятор собирает KengaOS.

В каждой версии часть файлов помечена `// TODO: migrate to Kenga`.

---

## Лицензия

MIT — как и сам Kenga-lang.

---

## Авторы

- **GermannM3** — язык Kenga, оригинальный roadmap.
- **KengaOS team** — реализация ядра и инфраструктуры.

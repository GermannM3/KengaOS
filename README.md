# KengaOS

<p align="center">
  <img src="docs/logo.png" alt="KengaOS" width="360"/>
</p>

<p align="center">
  <strong>KengaOS</strong> — операционная система нового поколения<br/>
  <em>64-битное ядро на языке Kenga для x86_64</em>
</p>

<p align="center">
  <a href="https://github.com/GermannM3/KengaOS/actions/workflows/ci.yml">
    <img src="https://github.com/GermannM3/KengaOS/actions/workflows/ci.yml/badge.svg" alt="CI"/>
  </a>
  <a href="https://github.com/GermannM3/KengaOS/actions/workflows/release.yml">
    <img src="https://github.com/GermannM3/KengaOS/actions/workflows/release.yml/badge.svg" alt="Release"/>
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-2ee6d6?labelColor=12151a" alt="MIT"/>
  </a>
  <a href="https://github.com/GermannM3/kenga-lang">
    <img src="https://img.shields.io/badge/kenga--lang-pinned-5b9dff?labelColor=12151a" alt="Kenga Language"/>
  </a>
</p>

---

## О проекте

**KengaOS** — операционная система нового поколения, написанная на системном языке [Kenga](https://github.com/GermannM3/kenga-lang). Ядро (кроме точки входа на ассемблере и нескольких FFI-стабов) написано на Kenga и компилируется в freestanding C через `kenga emit-c --freestanding`. Загрузка — через [Limine](https://limine-bootloader.org).

Возможности:
- 64-битное ядро (x86_64, long mode, HHDM higher-half)
- Графический вывод через линейный framebuffer (1280×800+)
- PS/2 клавиатура и мышь
- Многозадачность с round-robin планировщиком
- Процессы и IPC (send/recv)
- Kernel heap из физической памяти
- Обработка исключений и panic
- Интерактивный shell
- CI/CD для ISO и 7 toolchain-таргетов

---

## Быстрый старт

```bash
git clone --recursive https://github.com/GermannM3/KengaOS.git
cd KengaOS
./scripts/build.sh          # Linux / macOS
scripts\build.cmd           # Windows (MSYS2 / Git Bash)
```

Запуск в QEMU:

```bash
qemu-system-x86_64 -M q35 -cdrom build/kengaos.iso -serial stdio
```

---

## Скриншоты

Экран загрузки ядра (linear framebuffer, рисуется ядром на Kenga):

![Экран загрузки KengaOS](docs/boot-fb.png)

Интерактивный shell (PS/2 клавиатура → framebuffer-консоль):

![KengaOS shell](docs/console-shell.png)

---

## Возможности

| Компонент | Статус | Описание |
|---|---|---|
| Загрузчик | есть | Limine 12.6.0, `.limine_requests` |
| Архитектура | есть | 64-бит long mode + HHDM |
| UART 16550 | есть | Порт ввода-вывода (не MMIO) |
| Видео | есть | Linear framebuffer + 8×8 шрифт |
| Мышь (PS/2) | есть | Polling 0x60/0x64, XOR-курсор |
| GDT + IDT | есть | Исключения + panic (UART + framebuffer) |
| Планировщик | есть | Round-robin multitasking, собственные стеки |
| Клавиатура (PS/2) | есть | IRQ1, ring buffer, framebuffer-консоль |
| Shell | есть | `help`, `info`, `clear`, `echo`, `mem`, `ps`, `log`, `tasks` |
| Память | есть | Физическая память + kernel heap (~62 МБ) |
| Процессы + IPC | есть | `k_proc_spawn`, `k_ipc_send`/`k_ipc_recv`, очереди сообщений |
| CI/CD | есть | Автосборка ISO + smoke-тест в QEMU |
| Release | есть | Мультиплатформенный (7 таргетов) |

---

## Дорожная карта

Фаза 2 — прерывания и планировщик:
- PIT-таймер и регулярные IRQ
- Прерывный (timer-driven) планировщик (сейчас — кооперативный)
- Потоки и IPC-каналы

Фаза 3 — процессы и память:
- Процессы с изоляцией адресного пространства
- Page allocator и paging
- IPC-каналы между процессами
- Init daemon

Фаза 4+ — расширенный функционал:
- Сетевой стек (TCP/IP)
- Файловая система (FAT32 / ext2)
- Desktop environment с window manager
- Пакетный менеджер

---

## Сборка

Требования: C-компилятор (clang или gcc), линкер (`ld.lld` или GNU ld), `xorriso` (для ISO), QEMU (опционально). Компилятор Kenga подключается как git submodule.

```bash
./scripts/build.sh
```

Этапы:
1. Сборка компилятора Kenga (`cargo build --release`)
2. Эмиссия C-кода (`kenga emit-c --freestanding`)
3. Ассемблирование и компиляция (freestanding, без libc)
4. Линковка ELF (higher-half kernel)
5. Загрузка Limine 12.6.0 и создание ISO
6. Smoke-тест в QEMU (проверка UART-маркеров)
7. Готово — ISO для флешки или VM

---

## Структура репозитория

```
KengaOS/
├── kernel/                  # Ядро
│   ├── kmain.kenga         # Основной код (Kenga)
│   ├── start.S             # Точка входа (Assembly)
│   ├── kf_fb.c             # Framebuffer + консоль
│   ├── kf_kbd.c            # Клавиатура (PS/2)
│   ├── kf_mem.c            # Физическая память + heap
│   ├── kf_proc.c           # Процессы + IPC
│   ├── kf_shell.c          # Интерактивный shell
│   ├── intr.c / isr.S      # GDT + IDT + обработчики
│   ├── sched.c             # Планировщик
│   ├── linker.ld           # Скрипт компоновщика
│   └── limine.cfg          # Конфигурация загрузчика
├── scripts/                # build.sh / build.cmd
├── docs/                   # Логотип и скриншоты
├── .github/workflows/      # CI + release
└── kenga-lang/             # Компилятор Kenga (submodule)
```

---

## Язык Kenga

**Kenga** — компактный строго типизированный системный язык для системного программирования:
- Семверный компилятор с чистым `emit-c` бэкендом (в т.ч. `--freestanding`)
- Встроенные интринсики для доступа к аппаратуре (`asm_inb/outb`, `mmio_read8/write8`)
- Атрибут `@intrinsic` для FFI
- Таргеты для Linux, macOS, Windows, Android, iOS

```kenga
@intrinsic fn asm_outb(port: i64, value: i64);

fn uart_write(c: i64) {
    asm_outb(0x3f8, c);
}
```

---

## Участие в проекте

KengaOS — активно развивающийся проект. Приветствуются:
- Отчеты об ошибках через Issues
- Предложения улучшений
- Code review и PR
- Улучшения документации

---

## Лицензия

[MIT](LICENSE) © [GermannM3](https://github.com/GermannM3)

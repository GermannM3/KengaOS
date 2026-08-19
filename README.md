<p align="center">
  <img src="docs/banner.png" alt="KengaOS" width="920"/>
</p>

<p align="center">
  <h1>⚡ KengaOS</h1>
  <strong>Операционная система нового поколения</strong><br/>
  <em>64-битное ядро на языке Kenga с полноценной архитектурой для x86_64</em>
</p>

<p align="center">
  <a href="https://github.com/GermannM3/KengaOS/actions/workflows/ci.yml">
    <img src="https://github.com/GermannM3/KengaOS/actions/workflows/ci.yml/badge.svg" alt="CI Status"/>
  </a>
  <a href="https://github.com/GermannM3/KengaOS/actions/workflows/release.yml">
    <img src="https://github.com/GermannM3/KengaOS/actions/workflows/release.yml/badge.svg" alt="Release"/>
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-2ee6d6?labelColor=12151a" alt="MIT License"/>
  </a>
  <a href="https://github.com/GermannM3/kenga-lang">
    <img src="https://img.shields.io/badge/kenga--lang-pinned-5b9dff?labelColor=12151a" alt="Kenga Language"/>
  </a>
</p>

---

## 🚀 Что это?

**KengaOS** — это передовая операционная система нового поколения, разработанная на современном системном языке **Kenga**. Это не очередной минимальный проект — здесь реализована полноценная архитектура с поддержкой:

- ✨ **64-битное ядро** с advanced memory management
- 🎮 **Графический интерфейс** с линейным framebuffer (1280×800+)
- 🖱️ **Поддержка PS/2** устройств (клавиатура и мышь)
- 🔄 **Многозадачность** с round-robin планировщиком
- 💾 **Динамическая память** с kernel heap allocator
- 🛡️ **Обработка исключений** и защита от сбоев
- ⚙️ **Интерактивный shell** с полноценной командной строкой
- 🔧 **CI/CD** пайплайны с автоматической сборкой для 7 платформ

---

## ⚡ Быстрый старт (30 секунд)

### Клонирование и сборка

```bash
git clone --recursive https://github.com/GermannM3/KengaOS.git
cd KengaOS
./scripts/build.sh          # Linux / macOS
scripts\build.cmd           # Windows (MSYS2 / Git Bash)
```

### Запуск в QEMU

```bash
qemu-system-x86_64 -M q35 -cdrom build/kengaos.iso -serial stdio
```

---

## 📸 Скриншоты

### Экран загрузки ядра
**Linear framebuffer 1280×800 с прямой рисовкой из ядра на Kenga:**

![Экран загрузки KengaOS](docs/boot-fb.png)

### Интерактивный shell
**PS/2 клавиатура → framebuffer-консоль с полноценным командным интерпретатором:**

![KengaOS shell](docs/console-shell.png)

---

## ✅ Текущие возможности (M1)

| 🎯 Компонент | Статус | Описание |
|---|---|---|
| **Загрузчик** | ✅ | Limine 12.6.0 (stivale2-free) с `.limine_requests` |
| **Architecture** | ✅ | 64-бит long mode + HHDM higher-half mapping |
| **UART 16550** | ✅ | Серийный порт (I/O ports, не MMIO) для отладки |
| **Видеосистема** | ✅ | Linear framebuffer + 8×8 bitmap-шрифт |
| **Мышь (PS/2)** | ✅ | Polling 0x60/0x64 с XOR-курсором |
| **GDT + IDT** | ✅ | Дескрипторные таблицы с красивым panic |
| **Планировщик** | ✅ | Round-robin multitasking с собственными стеками |
| **Клавиатура (PS/2)** | ✅ | IRQ1 с ring buffer + framebuffer-консоль |
| **Shell** | ✅ | `help`, `info`, `clear`, `echo`, `mem`, `tasks` |
| **Память** | ✅ | Физическая память + kernel heap (~62 МБ) |
| **Allocator** | ✅ | Bump-аллокатор с FFI в C |
| **Error Handling** | ✅ | Panic / oops обработчики |
| **CI/CD** | ✅ | Автосборка ISO + smoke-тест в QEMU |
| **Release** | ✅ | Мультиплатформенный релиз (7 таргетов) |

---

## 🗺️ Дорожная карта (M2+)

### Фаза 2: Прерывания и Timer-driven планировщик
- ⏱️ **PIT-таймер** — регулярные прерывания для планировщика
- 🔄 **Превентивный scheduling** — вытесняющая многозадачность
- 🧵 **Потоки и IPC** — межпроцессное взаимодействие

### Фаза 3: Процессы и расширенная память
- 📦 **Процессы** с изоляцией памяти
- 🔐 **Page allocator** с paging support
- 💬 **IPC-каналы** для коммуникации между процессами
- 🐚 **Init daemon** — системный инициализатор

### Фаза 4+: Расширенный функционал
- 📡 **Сетевой стек** (TCP/IP)
- 💾 **Файловая система** (FAT32 / ext2)
- 🎨 **Desktop environment** с window manager
- 📦 **Package manager**

---

## 🛠️ Сборка

### Требования

Для сборки нужен:
- **C-компилятор** (clang или gcc)
- **Linker** (`ld.lld` или GNU ld)
- **xorriso** (для создания ISO)
- **QEMU** (опционально, для тестирования)

Kenga-компилятор автоматически скачивается как git submodule.

### Процесс сборки (7 этапов)

```bash
./scripts/build.sh
```

1. 🔨 Компиляция компилятора Kenga (`cargo build --release`)
2. 📝 Эмиссия C-кода (`kenga emit-c --freestanding`)
3. 🔧 Ассемблирование и компиляция (freestanding, без libc)
4. 🔗 Линковка ELF-бинарника (higher-half kernel)
5. 📀 Скачивание Limine 12.6.0 и создание ISO
6. 🧪 Smoke-тест в QEMU с проверкой UART-маркеров
7. ✅ Готово — ISO для флешки или VM

---

## 📁 Структура репозитория

```
KengaOS/
├── kernel/                  # Ядро операционной системы
│   ├── kmain.kenga         # Основной код ядра (Kenga)
│   ├── start.S             # Boot entry point (Assembly)
│   ├── linker.ld           # Скрипт компоновщика
│   └── limine.cfg          # Конфигурация загрузчика
│
├── scripts/                 # Скрипты сборки
│   ├── build.sh            # Linux / macOS
│   └── build.cmd           # Windows (MSYS2)
│
├── docs/                    # Документация и скриншоты
│   ├── banner.png
│   ├── boot-fb.png
│   └── console-shell.png
│
├── .github/workflows/       # CI/CD пайплайны
│   ├── ci.yml              # Автосборка и тестирование
│   └── release.yml         # Мультиплатформенный релиз
│
└── kenga-lang/             # Компилятор Kenga (submodule)
```

---

## 🎓 Язык Kenga

**Kenga** — компактный, строго типизированный системный язык нового поколения, разработанный специально для системного программирования:

### Ключевые особенности

| Возможность | Описание |
|---|---|
| **Семверный компилятор** | Чистый `emit-c` бэкенд для bare metal |
| **Freestanding режим** | `kenga emit-c --freestanding` для ядра |
| **Встроенные интринсики** | `asm_inb/outb`, `mmio_read8/write8` для hardware access |
| **FFI атрибуты** | `@intrinsic` для импорта/экспорта C-функций |
| **Кросс-платформенность** | Таргеты для Linux, macOS, Windows, Android, iOS |
| **Строгая типизация** | Надежный и предсказуемый код |
| **Нулевой overhead** | Компилируется в оптимальный C/машинный код |

### Примеры на Kenga

```kenga
// Интринсик для доступа к портам
@intrinsic fn asm_outb(port: u16, value: u8) -> void

// Функция ядра
fn uart_write(c: u8) -> void {
    asm_outb(0x3f8, c)  // UART-порт
}
```

---

## 🤝 Участие в проекте

KengaOS — это активно развивающийся проект! Мы приветствуем:

- 🐛 **Отчеты об ошибках** через Issues
- 💡 **Предложения улучшений** и идеи
- 🔍 **Code review** и PR с новыми фичами
- 📚 **Улучшения документации**

---

## 📄 Лицензия

[MIT](LICENSE) © [GermannM3](https://github.com/GermannM3)

Разработано с ❤️ для сообщества системного программирования.

---

<p align="center">
  <strong>⭐ Если проект вам нравится, поставьте звезду!</strong>
  <br/>
  <em>KengaOS — это ОС нового поколения на современном языке</em>
</p>

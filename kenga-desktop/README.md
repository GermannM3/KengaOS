# KengaOS Desktop Environment

Нативный десктопный UI для KengaOS, написанный на языке Kenga. Полностью соответствует дизайну из HTML-референса.

## Структура проекта

```
kenga-desktop/
├── src/
│   ├── main.kg              # Точка входа, главный цикл desktop
│   ├── graphics/
│   │   └── renderer.kg      # Native renderer с double buffering
│   ├── ui/
│   │   └── components.kg    # UI компоненты (TopBar, Dock, Windows)
│   └── system/
│       ├── events.kg        # Система событий (мышь, клавиатура)
│       └── wm.kg            # Window manager
└── README.md
```

## Компоненты

### 🖥️ Renderer (`graphics/renderer.kg`)
- Двойная буферизация для плавного рендеринга
- Поддержка GPU acceleration (если доступно)
- Alpha blending и градиенты
- Размытые круги для glow эффектов
- Vignette overlay
- Работа с шрифтами (Inter, Unbounded, JetBrains Mono)

### 🎨 UI Components (`ui/components.kg`)
- **TopBar** - верхняя панель с часами, датой, RAM/CPU метриками
- **Dock** - боковая панель приложений с индикаторами запуска
- **Window** - система окон с трафик-кнопками, фокусом, перетаскиванием
- **TerminalWindow** - рабочий терминал с командами (help, ver, mem, clear, demo)
- **Launcher** - лаунчер приложений с поиском (F1)
- **BootScreen** - экран загрузки с логами и прогресс-баром
- **LockScreen** - экран блокировки с часами

### ⌨️ Event System (`system/events.kg`)
- Обработка мыши (клик, перетаскивание, dblclick)
- Обработка клавиатуры
- Callback система
- Модификаторы (Ctrl, Shift, Alt)

### 🪟 Window Manager (`system/wm.kg`)
- Z-order управление
- Фокус окон
- Перетаскивание окон
- Bring to front

## Дизайн-система

Все цвета и параметры взяты из HTML-референса:

```
COLOR_ACCENT    = #8b7bff (фиолетовый акцент)
COLOR_ACCENT2   = #22d3ee (голубой акцент)
COLOR_BG        = #04060b (темный фон)
COLOR_TEXT      = #e8ecf8 (основной текст)
GLASS_ALPHA     = 0.78 (прозрачность glassmorphism)
```

Шрифты:
- **Inter** - основной UI шрифт
- **Unbounded** - заголовки, логотип, часы
- **JetBrains Mono** - терминал, код, метрики

## Архитектура

```
┌─────────────────────────────────────────────────────┐
│                    main.kg                          │
│              Desktop Environment Loop               │
├─────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌───────────┐ │
│  │  Renderer    │  │  Components  │  │   Events  │ │
│  │  (Graphics)  │  │     (UI)     │  │  (Input)  │ │
│  └──────────────┘  └──────────────┘  └───────────┘ │
│                        │                            │
│                  ┌─────┴─────┐                      │
│                  │    WM     │                      │
│                  │ (Windows) │                      │
│                  └───────────┘                      │
└─────────────────────────────────────────────────────┘
                        │
                        ▼
              ┌─────────────────┐
              │   Kenga Kernel  │
              │   (Hardware)    │
              └─────────────────┘
```

## Главный цикл

```kenga
fn main() {
    var desktop = Desktop::new()
    desktop.init()
    desktop.run_boot_sequence()
    
    while !desktop.is_shutdown {
        desktop.process_events()  // Обработка ввода
        desktop.update()          // Логика (RAM/CPU, анимации)
        desktop.render()          // Рендеринг
        Renderer.sync_to_vsync()  // Синхронизация
    }
    
    desktop.shutdown()
}
```

## Функционал

### Boot Sequence
- Пошаговая загрузка с логами
- Прогресс-бар с градиентом
- Плавное затухание

### Окна
- Перетаскивание за title bar
- Трафик-кнопки (close/minimize/maximize)
- Фокус с glow эффектом
- Тени и скругления

### Терминал
- Команды: `help`, `ver`, `mem`, `clear`, `demo`
- История команд
- Мигающий курсор
- Цветной вывод

### Launcher (F1)
- Поиск приложений
- Сетка иконок
- Категории

### Анимации
- Drift эффект для glow orbs
- Blink для курсора и hint
- Pop-in для окон
- Fade-in/out для переходов

## Сборка

```bash
# Компиляция через Kenga compiler
kenga build src/main.kg --output kenga-desktop.bin

# Запуск в эмуляторе
kenga run kenga-desktop.bin

# Или напрямую на железе
kenga flash kenga-desktop.bin --target hardware
```

## Зависимости от ядра

Модуль требует следующие kernel API:

- `kernel/hardware.kg` - доступ к GPU, мышке, клавиатуре
- `kernel/memory.kg` - выделение памяти для буферов

## Мобильная адаптация

Для мобильной версии создайте отдельный модуль:

```
kenga-mobile/
├── src/
│   ├── main.kg           # Mobile entry point
│   └── mobile_components.kg  # Адаптированные компоненты
```

Изменения для mobile:
- Dock перемещается вниз (горизонтальный)
- Уменьшенные размеры элементов
- Скрытие live log панели
- Адаптивная сетка launcher (3 колонки)

## Лицензия

Часть KengaOS. Лицензия та же что и у ядра.

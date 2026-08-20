# KengaOS Self-Hosting

## Архитектурное правило (KengaOS 0.5+)

C — только bootstrap и низкоуровневый host там, где без него пока невозможно
загрузиться или обслужить железо. Вся новая функциональность ОС пишется на
Kenga.

Правило по умолчанию: каждый новый системный компонент KengaOS реализуется на
Kenga. Использование C для нового компонента требует явного обоснования
категорией A или временным ограничением категории C.

Если C-файл в ответ на вопрос «почему это всё ещё C?» отвечает:

- «потому что железо» — остаётся временно (категория A);
- «так быстрее было написать» — переписывается на Kenga (категория B);
- «Kenga пока не умеет X» — добавляем X в Kenga (категория C).

Стратегическая цель: Kenga постепенно съедает собственный C-host по всему
стеку — ядро, приложения, runtime, compiler. C должен превращаться в тонкий
слой железа, а не в скрытый второй язык KengaOS.

## Две линии self-hosting

1. Компилятор. Kenga генерирует свой нативный runtime:
   `kenga/emit/bc_src_c.kenga` (3053 строки) → `bootstrap/generated/bc_from_*.c`
   → `kenga_lite.c`. Bootstrap-цикл Kenga уже работает.
2. Ядро и ОС. KengaOS: kmain.kenga — это уже Kenga (243 строки; UART полностью
   на Kenga: uart_init/uart_putc/uart_puts, asm_outb/asm_inb). Дальше —
   десктоп, agents, model, apps.

## Аудит KengaOS 0.4 (A/B/C)

### A — оставить C сейчас (железо/загрузка)

| Модуль | Почему C |
|---|---|
| start.S | загрузчик, HHDM, limine-запросы |
| intr.c / isr.S | IDT/GDT/исключения, asm-обработчики |
| sched.c | контекстный свитч задач (iretq), asm |
| kf_kbd.c | PS/2 IRQ1, чтение порта 0x60 |
| kf_mouse.c | PS/2 IRQ12, пакеты мыши |
| kf_time.c | PIT, IRQ0 |
| kf_hw.c | cpuid / rtc (asm-инструкции) |
| kf_power.c | порты reboot/shutdown |
| kf_mem.c | физ. память, frame-битмап, heap |

### B — переписать на Kenga (логика, не железо)

| Модуль | Текущее | Замена |
|---|---|---|
| kf_gui.c | 296 строк C | desktop/agents/model/system на Kenga (0.6) |
| kf_shell.c | shell на C | shell на Kenga (fb + kbd уже есть intrinsic) |
| kf_vfs.c | initrd-разбор + таблица файлов | Kenga: strings/lists/struct |
| kf_proc.c (часть) | дерево процессов, IPC, caps | Kenga: списки + struct |
| kf_model.c | MLP 2-2-1 XOR на C | Kenga: в языке уже есть tensor/matmul (ml_host.kenga, native_ml.kenga) |

### C — сначала расширить Kenga

| Возможность | Где нужно | Что добавить |
|---|---|---|
| cpuid intrinsic | kf_hw.c | @intrinsic fn cpu_* или asm-обёртка |
| struct → C layout | процессы/VFS | struct уже есть (native_struct.kenga), проверить emit в C |
| указатели/адреса | heap, IPC | mmio + asm уже есть; нужен честный ptr-тип |
| произвольная память | frame-аллокатор в Kenga | решено не делать сейчас (A) |

## GUI: 0.4 остаётся как есть

`ab3442f` / `5c528cd` / `096f7ba` не откатывать. KengaOS 0.4 = доказательство
концепции GUI на C-host. Следующий вопрос не «как красивее», а «может ли этот
GUI быть написан на Kenga».

## KengaOS 0.5 — язык, достаточный для UI

0.5 — это не новый UI, а создание языка, достаточного для написания UI:

- указатели (ptr): опасность «@intrinsic → C делает всё интересное»;
- intrinsics: framebuffer API, keyboard/mouse API;
- нативный struct layout (struct → C);
- минимальный `ui.kenga`: window, rect, text, line, fill, button, input,
  mouse, keyboard.

```kenga
fn button(x, y, w, h, label, on: i64) -> i64
fn window(x, y, w, h, title) -> i64
fn text(x, y, fg, s) -> i64
```

intrinsics-мост (уже частично есть в kmain.kenga): fb_rect, fb_text, fb_fill,
fb_xor, fb_cursor + новые kbd/mouse геттеры.

Цель: C остаётся тонким слоем железа, логика UI пишется на Kenga.

## KengaOS 0.6 — Kenga Desktop

Переписать на Kenga: desktop, agents, model, system. C остаётся только в самом
низу (A). Структура:

```text
desktop/
  desktop.kenga
  window.kenga
  button.kenga
  panel.kenga
  agent_view.kenga
  model_view.kenga
  system_view.kenga
```

Критерий готовности: desktop.kenga / agent_view.kenga / model_view.kenga
работают без C-логики UI.

## CAP_UI — после Kenga Desktop

Концепция правильная, но не торопиться. Сначала — работающий десктоп на Kenga.
После этого CAP_UI становится естественным следующим шагом:

```text
Agent
  PID, parent, capabilities, memory, IPC, model, UI
```

Агент может создавать собственные интерфейсы:

```kenga
let w = ui.window("Research")
let b = ui.button("Run")
let t = ui.text("Waiting...")
```

Это будет не API, придуманный заранее, а реально существующий системный
ресурс, которому добавили capability. Полная картина: researcher =
spawn { CAP_IPC, CAP_MODEL_INFER, CAP_FILES, CAP_UI }; logger =
spawn { CAP_LOG } (без UI). Это capability-based agent desktop.

## Главное направление

Сходятся три линии: compiler self-hosting, ML primitives, OS programming.

```text
Kenga language
   |
   |-- compiler self-hosting
   |-- ML primitives
   |-- OS programming
   |
   v
KengaOS
   |
   +-- Agents  Model  UI
   |
   v
Kenga runtime
```

Проверка: способен ли один язык — Kenga — пройти весь путь от железа и
runtime до нейромодели, агента и графического рабочего пространства. Только
после работающего стека — большая Kenga-модель как следующий уровень.

## Цель одного языка

```text
Kenga compiler
   |
   v
KengaOS
   |
   v
Kenga Desktop
   |
   v
Kenga Agent
   |
   v
Kenga Model
```

Один язык проходит через весь стек: вычисление, модель, агент, интерфейс,
операционная среда. Z остаётся за пределами KengaOS: Z — представление
параметров, Kenga — представление данных и среды исполнения. Соединение —
отдельный эксперимент позже, не архитектурная зависимость.

## Мобильный форм-фактор

Цель: один desktop для десктопа и мобильных. Текущий код — x86_64
(Limine + SeaBIOS VBE), телефоны — ARM64.

Пути (в порядке предпочтения):

- Нативный порт ядра на AArch64 (долгий, качественный). Свой загрузчик
  (U-Boot/QEMU на ARM), драйверы тача и дисплея. Приоритет — после
  доведения десктопа до совершенства.
- Сетевой стек + VNC/веб-терминал: телефон любого типа подключается к ПК и
  видит живой десктоп.
- Android-эмулятор (QEMU в Termux) — работает уже сейчас, медленно.
- iPhone: только эмуляция/веб — Apple не позволяет ставить свои ОС.

Реальный сценарий пользователя («подключил старый телефон к ПК и
установил») на практике = эмулятор или VNC до нативного ARM-порта.


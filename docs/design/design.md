# KengaOS Design Specification

Референс: HTML-макет KengaOS 0.5 (Qwen) — утверждённый визуальный дизайн.
Правило: точный порт. Дизайн не интерпретировать, не упрощать. Если Kenga не
умеет примитив — добавлять примитив в Kenga, а не заменять его примитивным
аналогом.

## Извлечённая визуальная система

Цвета:

- BG base: #04060b
- glass gradient: rgba(22,28,48,0.78) -> rgba(10,14,26,0.72)
- stroke / border: rgba(255,255,255,0.08)
- accent (primary): #8b7bff
- accent2 (cyan): #22d3ee
- traffic: close #ff5f57, min #febc2e, max #28c840
- text: #e8ecf8, muted rgba(255,255,255,0.4..0.6)
- ok/green: #7ee7a3, log cyan #7ee7d0, err #ff8f8f

Композиция (z-order, снизу вверх):

1. wallpaper — абстрактный AI/network фон (живое пространство, не плоский
   градиент): тёмная база + светящиеся области + узлы-точки
2. wallGlow — два больших blur-пятна (accent / accent2), медленный drift
3. vignette — радиальное затемнение по краям
4. topbar: h=38, полупрозрачный, blur. Слева: логотип + KENGAOS + chip
   «0.5 · agent-native». Центр: HH:MM:SS + дата. Справа: chips CPU/RAM-bar/
   uptime + кнопка питания
5. dock: слева по вертикальному центру, floating, radius 18, pad 10,
   иконки 44x44, gap 6, индикатор running (4px точка + glow), active-рамка
6. окна: radius 14, свободное расположение с перекрытием, titlebar 40 со
   светофорами (12px круги) + title 12px + иконка + tag, body
   полупрозрачный, фокус = accent-свечение рамки
7. liveLog: 320px, правый нижний угол, шапка «● ЖИВОЙ ЛОГ · IPC-ТРАФИК»,
   строки [время] A -> B · сообщение
8. оверлеи: launcher (полноэкранный blur, сетка 4x150, search), power menu,
   lock (часы 76px), toasts

Мобильная адаптация (max-width 820): dock -> снизу по центру, liveLog
скрыт, окна на всю ширину, окно терминала 44px titlebar, часы lock 52px.

Типографика: Inter (UI), Unbounded (display), JetBrains Mono (system).
Плотный, но не перегруженный интерфейс. Маленький текст.

## Карта REFERENCE -> Kenga primitive -> файл

| Reference-элемент | Kenga primitive | Файл |
|---|---|---|
| стеклянная панель | glass_panel (blend с фоном + stroke) | kernel/ui/primitives.kenga |
| прозрачность | fb_blend_rect (alpha-смешение, C graphics layer) | kernel/kf_fb.c |
| blur | fb_blur (ограниченный box-blur, C) / имитация | kernel/kf_fb.c |
| rounded corner | fb_rrect (скруглённый прямоугольник, C) | kernel/kf_fb.c |
| градиент | fb_grad_rect (линейный градиент, C) | kernel/kf_fb.c |
| свечение/тень окна | glow_rect (несколько blend-слоёв с затуханием) | kernel/ui/primitives.kenga |
| topbar | topbar(...) | kernel/ui/topbar.kenga |
| dock + иконки | dock(...) + icon_bitmap (16x16 паттерны) | kernel/ui/dock.kenga, kernel/ui/icon.kenga |
| окно | window(...) + titlebar + светофоры | kernel/ui/window.kenga |
| окно Агенты | agents_view(...) | desktop/agents.kenga |
| окно Монитор | monitor_view(...) + sparkline | desktop/monitor.kenga |
| окно Файлы | files_view(...) | desktop/files.kenga |
| терминал | terminal_view(...) | desktop/terminal.kenga |
| живой лог | log_panel(...) | desktop/log.kenga |
| лаунчер | launcher(...) | desktop/launcher.kenga |
| lock | lock_screen(...) | desktop/lock.kenga |
| тосты | toast(...) | desktop/toast.kenga |

## Приоритет реализации

1. композиция
2. размеры
3. расположение окон
4. фон
5. прозрачность
6. blur
7. цвета
8. типографика
9. иконки
10. взаимодействия

## Acceptance criteria

Screenshot KengaOS рядом с референсом должен выглядеть как один и тот же
продукт: тот же фон, та же композиция, те же окна с перекрытием, тот же
glass-эффект, те же панели. Наличие всех компонентов без визуального
совпадения успехом не считается.

## Новые примитивы, которые нужно добавить (C graphics layer + Kenga)

- fb_blend_rect(x,y,w,h,color,alpha) — alpha-смешение с текущим пикселем
- fb_rrect(x,y,w,h,r,color) — скруглённый прямоугольник (заливка)
- fb_grad_rect(x,y,w,h,c0,c1,vertical) — линейный градиент
- fb_glow(x,y,w,h,color,strength) — мягкое свечение (несколько слоёв blend)
- fb_blur(x,y,w,h,radius) — box blur области (для стекла за окнами)
- icon-формат: 16x16/24x24 bitmap в Kenga (hex-строки)

Всё остальное — на Kenga (ui/*.kenga, desktop/*.kenga). C — только
graphics primitives (категория A: fb-уровень).

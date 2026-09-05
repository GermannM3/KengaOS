# KengaOS → ARM64: путь к телефонам

Одно ядро — ПК и мобилка. Kenga-код (`kmain.kenga`, `desktop.kenga`) не меняется
ни на одной архитектуре: `emit-c --freestanding` даёт нейтральный C, а
арх-специфика живёт в тонком слое `kernel/aarch64/` (аналог `start.S`/`intr.c`/
`kf_hw.c`/`kf_time.c` для x86_64).

Текущее состояние: **этап 1 завершён** — ядро грузится на aarch64 (QEMU virt,
Limine v12 UEFI, base revision 6), Aurora-десктоп рисуется на framebuffer,
таймер/прерывания/BRK-кругосветка/кадровый аллокатор — ок.
Смоук: `scripts/run-a64.sh --headless` (гейт по UART-маркерам).

## Что уже есть (этап 1)

- `kernel/aarch64/`: start64.S (entry, Limine requests, MMIO-окно через
  TTBR0), vectors.S (VBAR_EL1, BRK-тест), intr_a64.c (GICv2), hw_a64.c
  (PL011 + 16550-порт-тень), time_a64.c (CNTV generic timer), input_a64.c
  (UART-консоль как клавиатура), power_a64.c (PSCI), sched_a64.c (cooperative
  заглушки), linker_a64.ld.
- `scripts/build-a64.sh` + `scripts/a64-sed.sed` (x86-asm в сгенерированном C
  → aarch64-хуки; правильное место — флаг `--arch` в kenga-lang, см. кодоген).
- `scripts/run-a64.sh` — QEMU virt + edk2-aarch64 + ESP (mtools) с
  `limine/BOOTAA64.EFI`.

Нюансы rev 6, которые важно знать (все уже учтены):
- вход возможен на EL2(+VHE) — есть защитный drop до EL1;
- CPACR_EL1 = 0 на входе — FPEN включаем в _start до любого NEON;
- Limine мапит только RAM/framebuffer — MMIO (UART/GIC) ядро мапит само
  через свободный TTBR0_EL1 (identity 1GiB Device-block);
- PA = VA - virtual_base + physical_base; таблицы этапа 1 живут по
  фиксированному PA 0x50000000 (пишутся через HHDM-вид);
- HHDM у v12 = 0xffff000000000000; response->offset лежит на +8.

## Дорожная карта

### Этап 2 — тач и мобильная оболочка (QEMU)
- Мобильная оболочка: **UI-референс готов** (`mobile.html`, `src/mobile/` —
  boot → lock (свайп) → home (виджет, сетка, dock) → лаунчер → приложения
  на весь экран; терминал рабочий). Запуск: `npm install && npm exec vite --`,
  затем `http://localhost:5173/mobile.html`.
- USB-тач: `kernel/aarch64/usb_a64.c` — минимальный xHCI для QEMU virt.
  Работает: ECAM-скан, HCRST (+ожидание CNR), command/event ring (поллинг),
  Enable Slot, Address Device. Останавливается: событие завершения
  control-передачи (GET_DESCRIPTOR) не всплывает в event ring, хотя QEMU
  трассирует xfer (см. `usb_xhci_queue_event`). Гипотеза: фаза DATA/STATUS
  в QEMU требует отдельного звонка в doorbell либо report уходит мимо ERDP.
  QEMU-флаги уже в `scripts/run-a64.sh` (`highmem-ecam=off`, qemu-xhci,
  usb-tablet). Подводные камни, которые уже найдены: PORTSC stride = 0x10,
  команды слотов = TRB-типы 9/11/12/13 (не 23+!), slot id в ctrl[31:24]
  event-TRB, add-flags ICC в dw1, кольца transfer'ов надо обнулять,
  DMA-буферы — только из кадрового аллокатора (стек/статика не
  hhdm-линейны).

### Этап 3 — телефонный трек

**Актуальный телефон — POCO M4 Pro (MediaTek Helio G96 / Dimensity 810).
Полный статус, план разблокировки (mtkclient) и порядок действий —
`docs/PHONE-TRACK.md`.** Для MTK-трека шаги 1/4 ниже в целом те же, но
DTB и GIC-версию берём из дерева устройства телефона (уточнить после adb).

#### Историческое: Oppo A5 2020 (CPH1931, Qualcomm SM6125 / Snapdragon 665)

Статус: **пауза** — загрузчик закрыт (OEM-unlock скрыт, Deep Testing
заявка подана, вероятность низкая). Возвращаемся, если Oppo одобрит.
Техническая справка (если вернёмся): SM6125 — один из самых
поддержанных Qualcomm-чипов в mainline Linux (референсы: Sony Xperia
10 II, postmarketOS), значит DTB и адреса устройств известны.

Порядок bring-up (каждый шаг проверяем отдельно):

1. **Загрузочный контейнер**: ядро gzip → Android boot.img (header v0/v2,
   размер под offset `0x4800000`-класс ABL) + DTB от mainline для sm6125
   (взять из postmarketOS/linux-sm6125). Прошивается ТОЛЬКО через
   `fastboot boot` — без сноса.
2. **Ранняя консоль**: на A5 2020 UART не распаян на разъёмы — отладка
   через framebuffer ( qualcomm ABL обычно выключает экран на handoff;
   оставляем включённым через сохранение configure'р ПК) либо
   USB-serial через dwc3 peripheral. Практично: рисовать прямо в
   фреймбуфер, оставшийся от загрузчика (simple-framebuffer).
3. **FDT-парсер** в ядре (старт написан): адрес UART, рам, фреймбуфер —
   из DTB, не хардкодом. Для QEMU virt — тоже DTB от QEMU (`-machine
   dumpdtb`) — единый код для эмулятора и железа.
4. **GICv3** вместо GICv2: SM6125 = GICv3 (GICD + GICR-редистрибьюторы,
   ICC_SRE_EL1; без ITS — SPI достаточно). Порт intr_a64.c.
5. **Тач/дисплей**: DSI-панель (mainline-драйвер для этой панели есть),
   тач I2C (goodix) — после GICv3+таймера.
6. **Чек-лист перед сносом** — как выше: дисплей, тач, Wi-Fi (wcn3990,
   ath10k-класс), BT, модем, зарядка.

Известные упрощения v1: одно ядро (BSP), без SMP, без ITS, eMMC потом.

### Этап 3-исторический — старый Oppo (первое реальное ARM64-железо)
Принцип: **ничего не сносим, пока чек-лист не зелёный.**
1. Определить точную модель/SoC (Settings → About или fastboot `getvar all`).
2. Разблокировать загрузчик (Oppo: обычно через unlock-инструмент/MTK/QCOM
   эксплойты в зависимости от модели; данные стираются при разблокировке).
3. `fastboot boot kengaos-a64.img` — **ramdisk-загрузка без прошивки**: после
   перезагрузки телефон снова в Android. Это и есть «пред-сносные тесты».
4. Порт-работа (по референсу mainline Linux для той же SoC): display
   (DRM/simple-framebuffer), touch (i2c), UART для отладки.
5. Чек-лист перед сносом (всё должно работать из ramdisk-загрузки):
   - [ ] дисплей + тач
   - [ ] Wi-Fi
   - [ ] Bluetooth
   - [ ] сотовая связь: 4G/5G data (модем — самый тяжёлый пункт; звонки/SMS
     ещё тяжелее — RIL поверх модема)
   - [ ] зарядка/батарея (fuel gauge), кнопки, вибро
   - [ ] датчики (акселерометр и пр.)
6. Только после зелёного чек-листа — `fastboot flash` разделов boot
   (снос Android = установка KengaOS вместо него).

Честное ожидание: дисплей+тач+UART — недели; Wi-Fi/BT — зависит от чипа
(у Qualcomm часто норм с mainline-референсами); модем 4G/5G и звонки —
самый длинный путь, у всех альтернативных ОС на телефонах именно здесь
основные пробелы.

### Этап 4 — Samsung S21
Exynos 2100 и Snapdragon 888 очень разные по mainline-поддержке; референс —
mainline Linux для той же SoC. Путь тот же: ramdisk-загрузка → драйверы →
чек-лист → снос. Samsung Knox/ой-контроль может осложнять разблокировку —
оценивается отдельно.

### Этап 5 — Apple (в последнюю очередь, как договорились)
Жёсткое ограничение: secure boot Apple подписывает всю цепочку. Реально
кастомное ядро — только на checkm8-железе (A10/T2 и старше) через pongoOS.
На современных iPhone установка своей ОС невозможна в принципе — это не
задача порта, а свойство платформы.

## Известные хвосты этапа 1 (не блокируют)
- `sched.c`/usermode на a64 — cooperative-заглушки (agents регистрируются,
  потоков нет). Апгрейд: port switch.S на aarch64.
- Мышь/тач — этап 2.
- Косметика kmain.kenga: строки «x86_64 / UART 16550 / Limine 12.6» на
  ARM-загрузке — лечится флагом `--arch` в kenga-lang (или параметром),
  когда дойдём до компилятора.
- CI-джоба a64 — добавить после стабилизации.

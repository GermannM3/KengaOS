# Телефонный трек — цель, статус, путь до fastboot boot

Обновлено: 2026-09-05 (сопряжение выполнено). Этот документ — канонический
статус по телефонам. План bring-up ядра (общий для любой SoC) — в
`docs/PORT-ARM64.md`.

## Кандидаты

| Телефон | SoC | Статус | Решение |
|---|---|---|---|
| **POCO M4 Pro 4G** | **MediaTek Helio G96 (MT6781)**, codename **fleur** (2201117PG) | подключен, adb по Wi-Fi работает | **основной трек** |
| Oppo A5 2020 (CPH1931RU) | Qualcomm SM6125 | загрузчик закрыт: OEM-unlock скрыт, Deep Testing заявка подана (вероятность низкая) | пауза; вернуть при одобрении Oppo |
| Samsung S21 | Exynos 2100 / SD888 | в резерве (Knox) | после POCO |

## Подтверждённое железо (adb, 2026-09-05)

- POCO M4 Pro 4G, `fleur`, MT6781 (Helio G96), arm64-v8a
- Android 13, MIUI/OS1.0 (V816.0.11.0.TKEEUXM), 6 ГБ RAM
- Загрузчик: **locked** (`flash.locked=1`, `verifiedbootstate=green`)
- Главная кнопка: Helio G96 — 2×Cortex-A76 + 6×A55; для ядра понадобится
  GICv3 (уточнить по DTB телефона) и raw arm64 Image-обёртка

## Этап «оболочка поверх Android» (текущий, данные не трогаем)

Пока владелец не сохранил данные — никакого unlock/flash. KengaOS Mobile
ставится как обычное приложение (WebView-обёртка мобильной оболочки),
потом его можно назначить домашним экраном.

- Исходники: `android/` (MainActivity + манифест), сборка
  `scripts/build-apk.sh` (aapt2 + d8 + apksigner, без gradle),
  артефакт `build/apk/kengaos-mobile.apk` (~95 КБ).
- Категория HOME в манифесте: Android предложит «КengaOS Mobile» как
  вариант домашнего экрана — назначение по желанию, всё обратимо.
- Блокер MIUI: `INSTALL_FAILED_USER_RESTRICTED` — нужно включить
  «Установка через USB» в «Для разработчиков» (попросит Mi-аккаунт/SIM)
  и подтверждать окно установки на экране телефона.

## Состояние adb (2026-09-05)

- USB-драйвера в Windows нет (Google INF не знает VID_2717) — не нужно:
  работает **беспроводная отладка**: `adb pair` (сделано) + mDNS
  авто-подключение (`_adb-tls-connect._tcp`).
- Адрес и код одноразовые; при повторном сопряжении — новый код с экрана
  телефона, подключение `adb connect IP:PORT` (порт с главного экрана
  «Отладка по USB (беспроводная)»).

## Путь до первого `fastboot boot` (порядок)

1. `adb pair` + `adb connect` — полная видимость телефона с ПК.
2. mtkclient: BROM → разблокировка загрузчика (стирает данные!).
3. Образ: ядро aarch64 (gzip, raw arm64 Image-обёртка) + DTB →
   `kengaos-phone.img` (`scripts/mkbootimg.py` готов, round-trip тест есть).
4. `fastboot boot kengaos-phone.img` — RAM-загрузка **без сноса**:
   перезагрузка = снова Android, ноль риска.
   Примечание: при `fastboot boot` RAM уже инициализирована загрузчиком
   телефона, DTB устройства передаётся ядру — DRAM-init не нужен.
5. Bring-up по чек-листу (`docs/PORT-ARM64.md`, этап 3): фреймбуфер →
   тач → Wi-Fi → BT → модем.
6. Только после зелёного чек-листа — `fastboot flash` (снос Android =
   установка KengaOS).

## Что уже готово на стороне ядра

- aarch64-ядро: грузится, Aurora-десктоп, FDT-парсер (память/UART),
  GICv2 + groundwork GICv3, ring 3, магазин, пророк — SMOKE OK в QEMU (CI).
- `scripts/mkbootimg.py` — packer Android boot image v0.
- MTK-специфика ещё **не** начата: GIC-версию и адреса устройств берём
  из DTB телефона (уточнить после adb).

## Не хватает (по порядку)

1. Доступ adb (ждёт IP:порт+код от владельца).
2. Raw arm64 Image-обёртка ядра (сейчас ELF для Limine; телефону нужен
   Image-формат: голова `arm64`, x0 = DTB).
3. GICv3 отладка в QEMU (`-machine gic-version=3`) — groundwork уже в
   `kernel/aarch64/intr_a64.c`.
4. Фреймбуфер от загрузчика (simple-framebuffer node в DTB).

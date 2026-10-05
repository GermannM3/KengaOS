#!/usr/bin/env bash
# Проверка ОБЕИХ платформ одной командой.
#
# Зачем: в 232-м круге выяснилось, что поломка сборки aarch64 (undefined symbol
# k_net_state_set) пролежала 18 КРУГОВ незамеченной. Правки шли в общих файлах
# (kmain.kenga, kf_pci.kenga), а собиралась только x86 — и вторая платформа
# разошлась с первой. Этот скрипт закрывает такой пропуск одной командой.
#
# Правило: после ЛЮБОЙ правки в общих файлах запускать его, даже если задача
# «очевидно x86-only». Стоимость проверки — минуты, стоимость пропуска — круги
# уверенности в неверном состоянии.
#
# Использование: bash scripts/check-both.sh
set -u
cd "$(dirname "$0")/.." || exit 1
ok=1

if bash build/build-x86.sh > build/check-x86.txt 2>&1; then
  echo "x86_64: OK (все гейты)"
else
  ok=0
  echo "x86_64: FAIL"
  grep -aE "ERROR|error:" build/check-x86.txt | head -3
fi

export PATH=/clang64/bin:/usr/bin:/mingw64/bin:/ucrt64/bin:$PATH
if bash scripts/build-a64.sh > build/check-a64.txt 2>&1; then
  echo "aarch64: сборка OK"
else
  ok=0
  echo "aarch64: сборка FAIL"
  grep -aE "error:|undefined symbol" build/check-a64.txt | head -3
fi

if bash scripts/run-a64.sh --headless > build/check-a64run.txt 2>&1 && grep -aq "SMOKE OK" build/check-a64run.txt; then
  echo "aarch64: SMOKE OK"
else
  ok=0
  echo "aarch64: SMOKE FAIL"
  tail -2 build/check-a64run.txt
fi

if [ "$ok" = 1 ]; then
  echo "ИТОГ: обе платформы зелёные"
  exit 0
else
  echo "ИТОГ: ЕСТЬ ПРОБЛЕМЫ (см. выше)"
  exit 1
fi

#!/usr/bin/env bash
# Замер с РАЗДЕЛЬНЫМИ логами.
#
# Зачем: 227-й круг показал, что при чтении общего build/uart.log после серии
# прогонов можно получить ОСТАТКИ предыдущего запуска (особенно если сборка
# упала до запуска QEMU). Из-за этого был сделан неверный вывод о «флуктуации».
# Здесь каждый прогон сохраняет свой лог отдельно, а сводка печатается по ним.
#
# Использование: bash scripts/measure.sh [число_прогонов] [шаблон_маркера]
# Пример:       bash scripts/measure.sh 4 'net live code=[0-9a-fx-]*'
set -u
N=${1:-3}
PAT=${2:-'net live code=[0-9a-fx-]*'}
cd "$(dirname "$0")/.." || exit 1
rm -rf build/measure && mkdir -p build/measure
for i in $(seq 1 "$N"); do
  if ! bash build/build-x86.sh > "build/measure/build-$i.txt" 2>&1; then
    grep -aE "ERROR|error:" "build/measure/build-$i.txt" | head -1
  fi
  # СВОЙ лог до следующего прогона — иначе прочитаем чужой.
  if [ -f build/uart.log ]; then cp build/uart.log "build/measure/uart-$i.log"; fi
  printf 'run%s exit-lines: ' "$i"
  if [ -f "build/measure/uart-$i.log" ]; then
    tr -d '\r' < "build/measure/uart-$i.log" | grep -aoE "$PAT" | tr '\n' ' '
  fi
  echo
done
echo '--- сводка ---'
for f in build/measure/uart-*.log; do
  [ -f "$f" ] || continue
  printf '%s: ' "$f"
  tr -d '\r' < "$f" | grep -acE "$PAT"
done

#!/usr/bin/env bash
# Замер с РАЗДЕЛЬНЫМИ логами прогонов.
#
# История инструмента:
#   227 круг — вывод о «флуктуации» оказался артефактом: читался общий uart.log,
#              оставшийся от предыдущего запуска после УПАВШЕЙ сборки;
#   228 круг — скрипт сохраняет лог каждого прогона отдельно;
#   230 круг — обнаружено, что при УПАВШЕЙ сборке скрипт всё равно показывал
#              маркеры: брал устаревший build/uart.log. Теперь при неудаче
#              сборки лог помечается как НЕДЕЙСТВИТЕЛЬНЫЙ и не читается.
#
# Использование: bash scripts/measure.sh [число_прогонов] [шаблон_маркера]
set -u
N=${1:-3}
PAT=${2:-'net live code=[0-9a-fx-]*'}
cd "$(dirname "$0")/.." || exit 1
rm -rf build/measure && mkdir -p build/measure
for i in $(seq 1 "$N"); do
  if bash build/build-x86.sh > "build/measure/build-$i.txt" 2>&1; then
    st=OK
  else
    st=FAIL
    grep -aE "ERROR|error:" "build/measure/build-$i.txt" | head -1
  fi
  if [ "$st" = OK ] && [ -f build/uart.log ]; then
    cp build/uart.log "build/measure/uart-$i.log"
    printf 'run%s %s: ' "$i" "$st"
    tr -d '\r' < "build/measure/uart-$i.log" | grep -aoE "$PAT" | tr '\n' ' '
    echo
  else
    : > "build/measure/uart-$i.log"          # НЕ читаем чужой лог
    echo "run$i $st: лог недостоверен, замер пропущен"
  fi
done
echo '--- сводка (пусто = сборка падала) ---'
for f in build/measure/uart-*.log; do
  [ -f "$f" ] || continue
  printf '%s: ' "$f"
  tr -d '\r' < "$f" | grep -acE "$PAT"
done

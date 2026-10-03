#!/usr/bin/env bash
# build-user.sh — Kenga-приложение -> ring-3 ELF (Kenga -> C -> ELF).
#
#   user/kenga_app.kenga --kenga emit-c --freestanding--> C
#     --patch-kenga-user.py (println -> int 0x80 write)-->
#     --clang --target=x86_64-elf--> .o --ld.lld--> build/user/kenga-app.elf
#
# ELF потом попадает в initrd (mkinitrd.py) как user-kenga.elf, ядро кладёт
# его в KengaFS /apps/kenga.elf и запускает в ring 3.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KENGA_ROOT="$ROOT/kenga-lang"
BUILD="$ROOT/build"
UDIR="$BUILD/user"
mkdir -p "$UDIR"

# --- компилятор Kenga ---
KENGA_BIN="$KENGA_ROOT/target/release/kenga"
if [[ -x "$KENGA_BIN" ]]; then :
elif [[ -x "$KENGA_BIN.exe" ]]; then KENGA_BIN="$KENGA_BIN.exe"
else echo 'error: kenga compiler not built (cargo build --release in kenga-lang)' >&2; exit 2
fi

# --- python для патча ---
PY=""
for cand in python python3 py; do
    if command -v "$cand" >/dev/null 2>&1; then PY="$cand"; break; fi
done
[[ -n "$PY" ]] || { echo 'error: python not found (needed for the print-stub patch)' >&2; exit 2; }

# --- C-компилятор/линкер (те же, что у ядра) ---
CC="${CC:-clang}"
LD="${LD:-ld.lld -flavor gnu -m elf_x86_64}"
CFLAGS="-ffreestanding -m64 -mcmodel=large -mno-red-zone -O2 -Wall"
CFLAGS="$CFLAGS -ffunction-sections -fdata-sections"
CFLAGS="$CFLAGS -nostdinc -I$ROOT/kernel/crt -I$ROOT/user"
if command -v clang >/dev/null 2>&1; then
    CFLAGS="$CFLAGS --target=x86_64-elf -isystem $(clang -print-resource-dir)/include"
fi

echo "[user] kenga emit-c --freestanding user/kenga_app.kenga"
"$KENGA_BIN" emit-c --freestanding "$ROOT/user/kenga_app.kenga" -o "$UDIR/kenga_app.c"
"$PY" "$ROOT/scripts/patch-kenga-user.py" "$UDIR/kenga_app.c"

eval "$CC $CFLAGS -c \"$UDIR/kenga_app.c\" -o \"$UDIR/kenga_app.o\""
eval "$CC $CFLAGS -c \"$ROOT/user/kenga_user_rt.c\" -o \"$UDIR/kenga_user_rt.o\""
# --strip-all: таблицы символов не грузятся, но входят в размер файла, а файл
# целиком читается с KengaFS в 4 КиБ буфер ядра (fs.dat). Без strip ELF
# раздувается вдвое и молча обрезается.
eval "$LD -nostdlib --no-rosegment --gc-sections --strip-all -e _start \"$UDIR/kenga_app.o\" \"$UDIR/kenga_user_rt.o\" -o \"$UDIR/kenga-app.elf\""
ls -la "$UDIR/kenga-app.elf"

# sstrip-lite: таблица секций/символов в образ не грузится, но раздувает файл,
# а файл целиком читается с KengaFS в 4 КиБ буфер ядра (st.dat).
"$PY" "$ROOT/scripts/elf-shrink.py" "$UDIR/kenga-app.elf" "$UDIR/kenga-app.shrunk"
mv -f "$UDIR/kenga-app.shrunk" "$UDIR/kenga-app.elf"
ls -la "$UDIR/kenga-app.elf"

ELF_SIZE=$(wc -c < "$UDIR/kenga-app.elf")
USERAPP_MAX=8192
if [[ "$ELF_SIZE" -gt "$USERAPP_MAX" ]]; then
    echo "error: kenga-app.elf is $ELF_SIZE bytes > $USERAPP_MAX (KengaFS exec buffer)" >&2
    echo "       уменьшите приложение или увеличьте k_xbuf (kernel/kf_blk.c)" >&2
    exit 1
fi
echo "[user] kenga-app.elf: $ELF_SIZE bytes (limit $USERAPP_MAX)"

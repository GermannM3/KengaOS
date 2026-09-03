#!/usr/bin/env bash
set -euo pipefail

# KengaOS aarch64 build — тот же исходник ядра, арх-слой из kernel/aarch64.
# Kenga-шаг идентичен build.sh (emit-c --freestanding), затем sed-слой
# (scripts/a64-sed.sed) переписывает x86-asm в сгенерированном C,
# дальше clang --target=aarch64-elf + ld.lld.
#
# x86-сборка (scripts/build.sh) не затрагивается.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/a64"
KERNEL_DIR="$ROOT/kernel"
A64_DIR="$KERNEL_DIR/aarch64"
KENGA_ROOT="$ROOT/kenga-lang"

mkdir -p "$BUILD_DIR"

export TEMP="${TEMP:-$BUILD_DIR}"
export TMP="${TMP:-$BUILD_DIR}"
export TMPDIR="${TMPDIR:-/tmp}"

log() { printf '\x1b[1;36m[a64 %s]\x1b[0m %s\n' "$1" "$2"; }

# ---------------------------------------------------------------------------
# 0. Kenga compiler (как в build.sh)
# ---------------------------------------------------------------------------
log "0/6" "Ensuring kenga compiler ($KENGA_ROOT)"
KENGA_BIN="$KENGA_ROOT/target/release/kenga"
if [[ -x "$KENGA_BIN" ]]; then
    :
elif [[ -x "$KENGA_BIN.exe" ]]; then
    KENGA_BIN="$KENGA_BIN.exe"
elif [[ -f "$KENGA_ROOT/Cargo.toml" ]]; then
    echo "building kenga compiler (cargo build --release) ..."
    (cd "$KENGA_ROOT" && cargo build --release)
    KENGA_BIN="$KENGA_ROOT/target/release/kenga"
    [[ -x "$KENGA_BIN" ]] || KENGA_BIN="$KENGA_BIN.exe"
elif command -v kenga >/dev/null 2>&1; then
    KENGA_BIN="kenga"
else
    echo "error: no kenga compiler and no kenga-lang/Cargo.toml next to this repo" >&2
    exit 2
fi

log "1/6" "Compiling kmain.kenga -> kmain.c via kenga emit-c --freestanding"
"$KENGA_BIN" emit-c --freestanding "$KERNEL_DIR/kmain.kenga" -o "$BUILD_DIR/kmain.c"

# ---------------------------------------------------------------------------
# Sed-слой: x86 asm -> aarch64-хуки (тот же C-исходник, другая арх-эmissions)
# ---------------------------------------------------------------------------
log "1b/6" "Rewriting x86 asm -> aarch64 hooks (scripts/a64-sed.sed)"
sed -f "$ROOT/scripts/a64-sed.sed" "$BUILD_DIR/kmain.c" > "$BUILD_DIR/kmain.a64.c"
for f in kf_fb kf_proc kf_shell; do
    sed -f "$ROOT/scripts/a64-sed.sed" "$KERNEL_DIR/$f.c" > "$BUILD_DIR/$f.a64.c"
done

# ---------------------------------------------------------------------------
# Toolchain: clang (clang64 в tools/msys64) --target=aarch64-elf + ld.lld
# ---------------------------------------------------------------------------
MSYS_BIN=""
for cand in "$ROOT/tools/msys64/clang64/bin" "$ROOT/../tools/msys64/clang64/bin"; do
    if [[ -x "$cand/clang.exe" || -x "$cand/clang" ]]; then MSYS_BIN="$cand"; break; fi
done
if [[ -n "$MSYS_BIN" ]]; then
    export PATH="$MSYS_BIN:$PATH"
fi

CC="${CC:-clang}"
command -v "$CC" >/dev/null 2>&1 || { echo "error: clang not found (need clang64 from tools/msys64)" >&2; exit 2; }

CFLAGS="-ffreestanding -O2 -Wall -Wextra --target=aarch64-elf"
# C-only: без libc, freestanding-заголовки clang.
CFLAGS_C="$CFLAGS -nostdinc -I$KERNEL_DIR -include kf_rt.h"
if [[ "$CC" == *clang* ]]; then
    RESDIR="$($CC -print-resource-dir)"
    CFLAGS_C="$CFLAGS_C -isystem $RESDIR/include"
fi

LD="${LD:-ld.lld}"
if command -v "$LD" >/dev/null 2>&1; then
    LD="$LD -m aarch64linux"
else
    echo "error: ld.lld not found" >&2
    exit 2
fi

# ---------------------------------------------------------------------------
# Kernel objects: арх-слой + общий код (тот же, что на x86)
# ---------------------------------------------------------------------------
log "2/6" "Assembling aarch64 entry + vectors"
eval "$CC -c $CFLAGS \"$A64_DIR/start64.S\" -o \"$BUILD_DIR/start64.o\""
eval "$CC -c $CFLAGS \"$A64_DIR/vectors.S\"  -o \"$BUILD_DIR/vectors.o\""

log "3/6" "Compiling arch layer (intr/hw/time/input/power/sched)"
eval "$CC -c $CFLAGS_C \"$A64_DIR/intr_a64.c\"   -o \"$BUILD_DIR/intr_a64.o\""
eval "$CC -c $CFLAGS_C \"$A64_DIR/hw_a64.c\"     -o \"$BUILD_DIR/hw_a64.o\""
eval "$CC -c $CFLAGS_C \"$A64_DIR/time_a64.c\"   -o \"$BUILD_DIR/time_a64.o\""
eval "$CC -c $CFLAGS_C \"$A64_DIR/input_a64.c\"  -o \"$BUILD_DIR/input_a64.o\""
eval "$CC -c $CFLAGS_C \"$A64_DIR/power_a64.c\"  -o \"$BUILD_DIR/power_a64.o\""
eval "$CC -c $CFLAGS_C \"$A64_DIR/sched_a64.c\"  -o \"$BUILD_DIR/sched_a64.o\""
eval "$CC -c $CFLAGS_C \"$A64_DIR/usb_a64.c\"   -o \"$BUILD_DIR/usb_a64.o\""
eval "$CC -c $CFLAGS_C \"$A64_DIR/disk_a64.c\"   -o \"$BUILD_DIR/disk_a64.o\""
eval "$CC -c $CFLAGS_C \"$A64_DIR/user_a64.c\"    -o \"$BUILD_DIR/user_a64.o\""

log "4/6" "Compiling shared kernel (same sources as x86_64)"
eval "$CC -c $CFLAGS_C \"$BUILD_DIR/kmain.a64.c\"    -o \"$BUILD_DIR/kmain.o\""
eval "$CC -c $CFLAGS_C \"$KERNEL_DIR/kf_mem.c\"      -o \"$BUILD_DIR/kf_mem.o\""
eval "$CC -c $CFLAGS_C \"$BUILD_DIR/kf_fb.a64.c\"    -o \"$BUILD_DIR/kf_fb.o\""
eval "$CC -c $CFLAGS_C \"$BUILD_DIR/kf_proc.a64.c\"  -o \"$BUILD_DIR/kf_proc.o\""
eval "$CC -c $CFLAGS_C \"$BUILD_DIR/kf_shell.a64.c\" -o \"$BUILD_DIR/kf_shell.o\""
eval "$CC -c $CFLAGS_C \"$KERNEL_DIR/kf_vfs.c\"      -o \"$BUILD_DIR/kf_vfs.o\""
eval "$CC -c $CFLAGS_C \"$KERNEL_DIR/kf_pkg.c\"      -o \"$BUILD_DIR/kf_pkg.o\""
eval "$CC -c $CFLAGS_C \"$KERNEL_DIR/kf_model.c\"    -o \"$BUILD_DIR/kf_model.o\""
eval "$CC -c $CFLAGS_C \"$KERNEL_DIR/kf_wallpaper.c\" -o \"$BUILD_DIR/kf_wallpaper.o\""
eval "$CC -c $CFLAGS_C \"$KERNEL_DIR/kf_font_aa.c\"  -o \"$BUILD_DIR/kf_font_aa.o\""
eval "$CC -c $CFLAGS_C \"$KERNEL_DIR/kf_design.c\"   -o \"$BUILD_DIR/kf_design.o\""
eval "$CC -c $CFLAGS_C \"$KERNEL_DIR/kf_design_adapter.c\" -o \"$BUILD_DIR/kf_design_adapter.o\""

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
log "5/6" "Linking kernel-a64.elf"
OBJS=("$BUILD_DIR/start64.o" "$BUILD_DIR/vectors.o"
      "$BUILD_DIR/intr_a64.o" "$BUILD_DIR/hw_a64.o" "$BUILD_DIR/time_a64.o"
      "$BUILD_DIR/input_a64.o" "$BUILD_DIR/power_a64.o" "$BUILD_DIR/sched_a64.o"
      "$BUILD_DIR/usb_a64.o" "$BUILD_DIR/disk_a64.o"
      "$BUILD_DIR/user_a64.o"
      "$BUILD_DIR/kmain.o" "$BUILD_DIR/kf_mem.o" "$BUILD_DIR/kf_fb.o"
      "$BUILD_DIR/kf_proc.o" "$BUILD_DIR/kf_shell.o" "$BUILD_DIR/kf_vfs.o" "$BUILD_DIR/kf_pkg.o"
      "$BUILD_DIR/kf_model.o" "$BUILD_DIR/kf_wallpaper.o" "$BUILD_DIR/kf_font_aa.o"
      "$BUILD_DIR/kf_design.o" "$BUILD_DIR/kf_design_adapter.o")
$LD -n -nostdlib -T "$A64_DIR/linker_a64.ld" "${OBJS[@]}" -o "$BUILD_DIR/kernel-a64.elf"

log "6/6" "OK: $BUILD_DIR/kernel-a64.elf"
ls -la "$BUILD_DIR/kernel-a64.elf"
echo "run: scripts/run-a64.sh"

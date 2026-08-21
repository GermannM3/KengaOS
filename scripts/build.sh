#!/usr/bin/env bash
set -euo pipefail

# KengaOS build script.
# Works both when this script lives at <root>/scripts/build.sh (repo layout,
# kenga-lang is a sibling directory/submodule) and when invoked from the old
# D:\KengaOS layout. Root is always one level above the script.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KENGA_ROOT="$ROOT/kenga-lang"
BUILD_DIR="$ROOT/build"
KERNEL_DIR="$ROOT/kernel"

mkdir -p "$BUILD_DIR"

# clang (Windows) needs a writable TEMP/TMP; nested bash sessions lose them.
export TEMP="${TEMP:-$BUILD_DIR}"
export TMP="${TMP:-$BUILD_DIR}"
export TMPDIR="${TMPDIR:-/tmp}"

log() { printf '\x1b[1;36m[%s]\x1b[0m %s\n' "$1" "$2"; }

# ---------------------------------------------------------------------------
# 0. Kenga compiler (kenga-lang submodule / sibling repo)
# ---------------------------------------------------------------------------
log "0/7" "Ensuring kenga compiler is built ($KENGA_ROOT)"
KENGA_BIN="$KENGA_ROOT/target/release/kenga"
if [[ -x "$KENGA_BIN" ]]; then
    : # already built
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

log "1/7" "Compiling kmain.kenga -> kmain.c via kenga emit-c --freestanding"
"$KENGA_BIN" emit-c --freestanding "$KERNEL_DIR/kmain.kenga" -o "$BUILD_DIR/kmain.c"

# ---------------------------------------------------------------------------
# Toolchain: C compiler + linker
# ---------------------------------------------------------------------------
CC="${CC:-}"
if [[ -z "$CC" ]]; then
    if command -v x86_64-elf-gcc >/dev/null 2>&1; then CC=x86_64-elf-gcc
    elif command -v clang >/dev/null 2>&1; then CC=clang
    elif command -v gcc >/dev/null 2>&1; then CC=gcc
    else
        echo "error: no C compiler (need x86_64-elf-gcc, clang, or gcc)" >&2
        exit 2
    fi
fi

CFLAGS="-ffreestanding -m64 -mcmodel=large -mno-red-zone -O2 -Wall -Wextra"

# clang targets COFF by default on Windows — force ELF.
if command -v clang >/dev/null 2>&1 && [[ "$CC" == *clang* ]]; then
    CFLAGS="$CFLAGS --target=x86_64-elf"
fi

# C-only flags: no libc, but clang's freestanding headers. -include kf_rt.h is
# NOT applied to start.S (a C header would break asm preprocessing).
CFLAGS_C="$CFLAGS -nostdinc -I$KERNEL_DIR -include kf_rt.h"
if command -v clang >/dev/null 2>&1 && [[ "$CC" == *clang* ]]; then
    RESDIR="$(clang -print-resource-dir)"
    CFLAGS_C="$CFLAGS_C -isystem \"$RESDIR/include\""
fi

LD="${LD:-}"
if [[ -z "$LD" ]]; then
    if command -v ld.lld >/dev/null 2>&1; then
        # clang64 ld.lld defaults to the PE flavor; enable ELF explicitly.
        LD="ld.lld -flavor gnu -m elf_x86_64"
    elif command -v x86_64-elf-ld >/dev/null 2>&1; then
        LD="x86_64-elf-ld"
    elif command -v ld >/dev/null 2>&1; then
        LD="ld -m elf_x86_64"
    else
        echo "error: no linker (need ld.lld, x86_64-elf-ld, or ld)" >&2
        exit 2
    fi
fi

# ---------------------------------------------------------------------------
# Kernel objects
# ---------------------------------------------------------------------------
log "2/7" "Assembling start.S ($CC)"
$CC -c $CFLAGS "$KERNEL_DIR/start.S" -o "$BUILD_DIR/start.o"

log "3/7" "Compiling kmain.c (freestanding, no libc) ($CC)"
eval "$CC -c $CFLAGS_C \"$BUILD_DIR/kmain.c\" -o \"$BUILD_DIR/kmain.o\""

log "4/7" "Compiling kf_mem.c (physical memory + heap) ($CC)"
if [[ -f "$KERNEL_DIR/kf_mem.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_mem.c\" -o \"$BUILD_DIR/kf_mem.o\""
else
    echo "warning: kf_mem.c missing, skipping"
fi

log "4b/7" "Compiling kf_fb.c (framebuffer driver) ($CC)"
if [[ -f "$KERNEL_DIR/kf_fb.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_fb.c\" -o \"$BUILD_DIR/kf_fb.o\""
else
    echo "warning: kf_fb.c missing, skipping"
fi

log "4c/7" "Compiling intr.c (GDT/IDT/panic) + isr.S (stubs) ($CC)"
if [[ -f "$KERNEL_DIR/intr.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/intr.c\" -o \"$BUILD_DIR/intr.o\""
fi
if [[ -f "$KERNEL_DIR/isr.S" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/isr.S\" -o \"$BUILD_DIR/isr.o\""
fi

log "4d/7" "Compiling sched.c (PIT + scheduler) ($CC)"
if [[ -f "$KERNEL_DIR/sched.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/sched.c\" -o \"$BUILD_DIR/sched.o\""
fi

log "4e/7" "Compiling kf_kbd.c (keyboard) ($CC)"
if [[ -f "$KERNEL_DIR/kf_kbd.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_kbd.c\" -o \"$BUILD_DIR/kf_kbd.o\""
fi

log "4f/7" "Compiling kf_shell.c (shell) ($CC)"
if [[ -f "$KERNEL_DIR/kf_shell.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_shell.c\" -o \"$BUILD_DIR/kf_shell.o\""
fi

log "4g/7" "Compiling kf_proc.c (processes + IPC) ($CC)"
if [[ -f "$KERNEL_DIR/kf_proc.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_proc.c\" -o \"$BUILD_DIR/kf_proc.o\""
fi

log "4h/7" "Compiling kf_vfs.c (vfs) ($CC)"
if [[ -f "$KERNEL_DIR/kf_vfs.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_vfs.c\" -o \"$BUILD_DIR/kf_vfs.o\""
fi

log "4i/7" "Compiling kf_time.c (timer) ($CC)"
if [[ -f "$KERNEL_DIR/kf_time.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_time.c\" -o \"$BUILD_DIR/kf_time.o\""
fi

log "4j/7" "Compiling kf_hw.c (cpuid/rtc) ($CC)"
if [[ -f "$KERNEL_DIR/kf_hw.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_hw.c\" -o \"$BUILD_DIR/kf_hw.o\""
fi

log "4k/7" "Compiling kf_power.c (reboot/shutdown) ($CC)"
if [[ -f "$KERNEL_DIR/kf_power.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_power.c\" -o \"$BUILD_DIR/kf_power.o\""
fi

log "4l/7" "Compiling kf_model.c (neural agent) ($CC)"
if [[ -f "$KERNEL_DIR/kf_model.c" ]]; then
    # -mno-sse: doubles use x87, safe from any stack alignment (click handler).
    eval "$CC -c $CFLAGS -mno-sse -mno-sse2 \"$KERNEL_DIR/kf_model.c\" -o \"$BUILD_DIR/kf_model.o\""
fi

log "4m/7" "Compiling kf_mouse.c (mouse) ($CC)"
if [[ -f "$KERNEL_DIR/kf_mouse.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_mouse.c\" -o \"$BUILD_DIR/kf_mouse.o\""
fi

log "4m2/7" "Compiling kf_usb.c (UHCI + usb-tablet) ($CC)"
if [[ -f "$KERNEL_DIR/kf_usb.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_usb.c\" -o \"$BUILD_DIR/kf_usb.o\""
fi

log "4m3/7" "Compiling kf_wallpaper.c (embedded reference wallpaper) ($CC)"
if [[ -f "$KERNEL_DIR/kf_wallpaper.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_wallpaper.c\" -o \"$BUILD_DIR/kf_wallpaper.o\""
fi

log "4m4/7" "Compiling kf_font_aa.c (Segoe UI anti-aliased font) ($CC)"
if [[ -f "$KERNEL_DIR/kf_font_aa.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_font_aa.c\" -o \"$BUILD_DIR/kf_font_aa.o\""
fi

log "4n/7" "Compiling kf_gui.c (desktop) ($CC)"
if [[ -f "$KERNEL_DIR/kf_gui.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_gui.c\" -o \"$BUILD_DIR/kf_gui.o\""
fi

log "4o/7" "Compiling kf_design.c (Aurora design primitives) ($CC)"
if [[ -f "$KERNEL_DIR/kf_design.c" ]]; then
    eval "$CC -c $CFLAGS \"$KERNEL_DIR/kf_design.c\" -o \"$BUILD_DIR/kf_design.o\""
fi

log "5/7" "Linking kengaos.elf ($LD)"
OBJS=("$BUILD_DIR/start.o" "$BUILD_DIR/kmain.o")
if [[ -f "$BUILD_DIR/kf_mem.o" ]]; then OBJS+=("$BUILD_DIR/kf_mem.o"); fi
if [[ -f "$BUILD_DIR/kf_fb.o" ]]; then OBJS+=("$BUILD_DIR/kf_fb.o"); fi
if [[ -f "$BUILD_DIR/intr.o" ]]; then OBJS+=("$BUILD_DIR/intr.o"); fi
if [[ -f "$BUILD_DIR/isr.o" ]]; then OBJS+=("$BUILD_DIR/isr.o"); fi
if [[ -f "$BUILD_DIR/sched.o" ]]; then OBJS+=("$BUILD_DIR/sched.o"); fi
if [[ -f "$BUILD_DIR/kf_kbd.o" ]]; then OBJS+=("$BUILD_DIR/kf_kbd.o"); fi
if [[ -f "$BUILD_DIR/kf_shell.o" ]]; then OBJS+=("$BUILD_DIR/kf_shell.o"); fi
if [[ -f "$BUILD_DIR/kf_proc.o" ]]; then OBJS+=("$BUILD_DIR/kf_proc.o"); fi
if [[ -f "$BUILD_DIR/kf_vfs.o" ]]; then OBJS+=("$BUILD_DIR/kf_vfs.o"); fi
if [[ -f "$BUILD_DIR/kf_time.o" ]]; then OBJS+=("$BUILD_DIR/kf_time.o"); fi
if [[ -f "$BUILD_DIR/kf_hw.o" ]]; then OBJS+=("$BUILD_DIR/kf_hw.o"); fi
if [[ -f "$BUILD_DIR/kf_power.o" ]]; then OBJS+=("$BUILD_DIR/kf_power.o"); fi
if [[ -f "$BUILD_DIR/kf_model.o" ]]; then OBJS+=("$BUILD_DIR/kf_model.o"); fi
if [[ -f "$BUILD_DIR/kf_mouse.o" ]]; then OBJS+=("$BUILD_DIR/kf_mouse.o"); fi
if [[ -f "$BUILD_DIR/kf_usb.o" ]]; then OBJS+=("$BUILD_DIR/kf_usb.o"); fi
if [[ -f "$BUILD_DIR/kf_wallpaper.o" ]]; then OBJS+=("$BUILD_DIR/kf_wallpaper.o"); fi
if [[ -f "$BUILD_DIR/kf_font_aa.o" ]]; then OBJS+=("$BUILD_DIR/kf_font_aa.o"); fi
if [[ -f "$BUILD_DIR/kf_gui.o" ]]; then OBJS+=("$BUILD_DIR/kf_gui.o"); fi
if [[ -f "$BUILD_DIR/kf_design.o" ]]; then OBJS+=("$BUILD_DIR/kf_design.o"); fi
$LD -n -nostdlib -T "$KERNEL_DIR/linker.ld" "${OBJS[@]}" -o "$BUILD_DIR/kengaos.elf"
ls -la "$BUILD_DIR/kengaos.elf"

# ---------------------------------------------------------------------------
# Limine binaries (auto-download on first use)
# ---------------------------------------------------------------------------
LIMINE_DIR="$ROOT/limine"
if [[ ! -f "$LIMINE_DIR/limine-bios.sys" || ! -f "$LIMINE_DIR/limine-bios-cd.bin" || ! -f "$LIMINE_DIR/limine-uefi-cd.bin" ]]; then
    log "limine" "downloading Limine v12.6.0 binaries -> $LIMINE_DIR"
    mkdir -p "$LIMINE_DIR"
    URL="https://github.com/Limine-Bootloader/Limine/releases/download/v12.6.0/limine-binary.tar.gz"
    TMPDL="$BUILD_DIR/limine-dl.tar.gz"
    if command -v curl >/dev/null 2>&1; then
        curl -fL "$URL" -o "$TMPDL"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$TMPDL" "$URL"
    else
        echo "error: need curl or wget to download Limine" >&2
        exit 2
    fi
    tar -xzf "$TMPDL" -C "$LIMINE_DIR" --strip-components=1 2>/dev/null || tar -xzf "$TMPDL" -C "$LIMINE_DIR"
    for f in limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin; do
        find "$LIMINE_DIR" -name "$f" -exec cp -f {} "$LIMINE_DIR" \; 2>/dev/null || true
    done
    rm -f "$TMPDL"
fi

# ---------------------------------------------------------------------------
# ISO
# ---------------------------------------------------------------------------
log "6/7" "Preparing Limine ISO"
rm -rf "$BUILD_DIR/iso_root"
mkdir -p "$BUILD_DIR/iso_root/boot"
cp "$BUILD_DIR/kengaos.elf" "$BUILD_DIR/iso_root/boot/kengaos.elf"
# Limine looks for boot/limine.conf (limine.cfg is the in-repo source name).
# Generate the initrd and tell Limine to load it as a module.
# Prefer python (Python314 installs python.exe only); skip the Microsoft Store
# python3 app-execution stub, which fails silently.
for cand in python python3 py; do
    if command -v "$cand" >/dev/null 2>&1 && [[ "$(command -v "$cand")" != /c/Users/*/AppData/Local/Microsoft/WindowsApps/* ]]; then
        PY="$cand"; break
    fi
done
PY="${PY:-python3}"
"$PY" "$ROOT/scripts/mkinitrd.py" "$BUILD_DIR/iso_root/boot/initrd.img" "$ROOT"
# Insert module_path into the /KengaOS kernel entry of the generated config.
sed '/kernel_path:/a\    module_path: boot():/boot/initrd.img' "$KERNEL_DIR/limine.cfg" > "$BUILD_DIR/iso_root/boot/limine.conf"
# limine-bios.sys lives in the ISO ROOT (that's where limine-bios-cd.bin looks for it).
cp "$LIMINE_DIR/limine-bios.sys" "$BUILD_DIR/iso_root/limine-bios.sys"
cp "$LIMINE_DIR/limine-bios-cd.bin" "$BUILD_DIR/iso_root/boot/limine-bios-cd.bin"
cp "$LIMINE_DIR/limine-uefi-cd.bin" "$BUILD_DIR/iso_root/boot/limine-uefi-cd.bin"

if command -v xorriso >/dev/null 2>&1; then
    xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
        --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label \
        "$BUILD_DIR/iso_root" -o "$BUILD_DIR/kengaos.iso" 2>/dev/null
    echo "wrote $BUILD_DIR/kengaos.iso"
else
    echo "warning: xorriso not found, ISO not created"
fi

# ---------------------------------------------------------------------------
# QEMU smoke test
# ---------------------------------------------------------------------------
log "7/7" "Smoke test: QEMU boot + UART capture (5s timeout)"
UART_LOG="$BUILD_DIR/uart.log"
: > "$UART_LOG"
QEMU_RAN=0
if command -v qemu-system-x86_64 >/dev/null 2>&1 && [[ -f "$BUILD_DIR/kengaos.iso" ]]; then
    # QEMU is a native Windows binary on MSYS — it needs a Windows-style path.
    if command -v cygpath >/dev/null 2>&1; then
        WIN_UART="$(cygpath -m "$UART_LOG")"
    elif [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* || "$(uname -s)" == CYGWIN* ]]; then
        WIN_UART="${UART_LOG#/}"
        WIN_UART="${WIN_UART%%/*}:${WIN_UART#*/}"
    else
        WIN_UART="$UART_LOG"
    fi
    timeout 5 qemu-system-x86_64 -M q35 -cdrom "$BUILD_DIR/kengaos.iso" \
        -serial "file:$WIN_UART" -display none -no-reboot -m 64 \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 || true
    QEMU_RAN=1
    echo "--- UART output ($UART_LOG) ---"
    cat "$UART_LOG"
    echo "--------------------------------"
elif ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "warning: qemu-system-x86_64 not found, smoke test skipped"
fi

# CI gate: the boot must have produced the BOOT / UART / FB markers.
if [[ "$QEMU_RAN" == 1 ]]; then
    ok=1
    grep -Eq "KengaOS.*booting" "$UART_LOG" || { echo "ERROR: BOOT marker missing" >&2; ok=0; }
    grep -q "Hello from Kenga kernel" "$UART_LOG" || { echo "ERROR: UART marker missing" >&2; ok=0; }
    grep -q "FB READY" "$UART_LOG" || { echo "ERROR: FB READY marker missing" >&2; ok=0; }
    grep -q "INTR READY" "$UART_LOG" || { echo "ERROR: INTR READY marker missing" >&2; ok=0; }
    grep -q "INT3 CAUGHT" "$UART_LOG" || { echo "ERROR: INT3 (IDT) marker missing" >&2; ok=0; }
    grep -q "SHELL READY" "$UART_LOG" || { echo "ERROR: SHELL READY marker missing" >&2; ok=0; }
    grep -q "PROC READY" "$UART_LOG" || { echo "ERROR: PROC READY marker missing" >&2; ok=0; }
    grep -q "MEM READY" "$UART_LOG" || { echo "ERROR: MEM READY marker missing" >&2; ok=0; }
    grep -Eq "initrd files=[1-9][0-9]*" "$UART_LOG" || { echo "ERROR: initrd/VFS marker missing" >&2; ok=0; }
    if [[ $ok == 1 ]]; then
        echo "OK: kernel booted — BOOT/UART/FB markers present"
    else
        exit 1
    fi
fi

echo
echo "Build artifacts in $BUILD_DIR"
ls -la "$BUILD_DIR"

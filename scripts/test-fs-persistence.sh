#!/usr/bin/env bash
# test-fs-persistence.sh — доказывает, что KengaFS переживает перезагрузку.
#
# Два QEMU-бута на ОДНОМ scratch-диске (маркер KENGARWTEST1 на LBA0 — только
# такой диск KengaFS считает своим и разрешает писать). Ожидаем:
#   boot 1 -> fs:boot=1, boot 2 -> fs:boot=2 (значение /boots на диске).
# Сначала соберите ISO: scripts/build.sh (или build-x86 через bundled MSYS2).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
ISO="$BUILD/kengaos.iso"
DISK="$BUILD/fs-persist.img"
QEMU="${QEMU:-qemu-system-x86_64}"

if [[ ! -f "$ISO" ]]; then echo "error: $ISO not found — run scripts/build.sh first" >&2; exit 2; fi
if ! command -v "$QEMU" >/dev/null 2>&1; then echo "error: $QEMU not found" >&2; exit 2; fi

mkdir -p "$BUILD"
dd if=/dev/zero of="$DISK" bs=1M count=64 status=none
printf 'KENGARWTEST1' | dd of="$DISK" bs=512 count=1 conv=notrunc status=none

boot() {
    local log="$1" wdisk wlog
    : > "$log"
    if command -v cygpath >/dev/null 2>&1; then
        wdisk="$(cygpath -m "$DISK")"; wlog="$(cygpath -m "$log")"
    else
        wdisk="$DISK"; wlog="$log"
    fi
    timeout 12 "$QEMU" -M pc -cdrom "$ISO" \
        -drive "file=$wdisk,format=raw,if=ide" \
        -serial "file:$wlog" -display none -no-reboot -m 64 \
        -device qemu-xhci -device usb-tablet \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 || true
}

boot "$BUILD/fs-boot1.log"
boot "$BUILD/fs-boot2.log"

fail() { echo "FAIL: $1" >&2; exit 1; }
grep -q "FS OK"      "$BUILD/fs-boot1.log" || fail "KengaFS не смонтировалась на boot 1"
grep -q "fs:boot=1"  "$BUILD/fs-boot1.log" || fail "boot 1: /boots != 1"
grep -q "FS OK"      "$BUILD/fs-boot2.log" || fail "KengaFS не смонтировалась на boot 2"
grep -q "fs:boot=2"  "$BUILD/fs-boot2.log" || fail "persistence потеряна: boot 2 != 2"

echo "OK: KengaFS persistence — один диск, boot 1 -> boot 2"

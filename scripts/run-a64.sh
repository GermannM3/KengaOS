#!/usr/bin/env bash
set -euo pipefail

# KengaOS aarch64 run — QEMU virt + edk2 UEFI + Limine (BOOTAA64.EFI).
#
#   scripts/run-a64.sh            — окно QEMU (gtk) + UART в stdin/stdout
#   scripts/run-a64.sh --headless — 25с смоук: UART в файл + gate по маркерам
#
# Диск: raw-образ с FAT (mtools) — \EFI\BOOT\BOOTAA64.EFI (Limine aarch64),
# /boot/kengaos.elf (наш kernel-a64.elf), /boot/limine.conf, /boot/initrd.img.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/a64"
KERNEL_ELF="$BUILD_DIR/kernel-a64.elf"
ESP_IMG="$BUILD_DIR/kengaos-a64.img"
LIMINE_DIR="$ROOT/limine"
KERNEL_DIR="$ROOT/kernel"

[[ -f "$KERNEL_ELF" ]] || { echo "error: $KERNEL_ELF not found — run scripts/build-a64.sh first" >&2; exit 1; }

# --- инструменты (tools/msys64 рядом с репо) ---
MSYS_UCRT=""
for cand in "$ROOT/tools/msys64/ucrt64/bin" "$ROOT/../tools/msys64/ucrt64/bin"; do
    [[ -x "$cand/qemu-system-aarch64.exe" || -x "$cand/qemu-system-aarch64" ]] && { MSYS_UCRT="$cand"; break; }
done
export PATH="${MSYS_UCRT:+$MSYS_UCRT:}$PATH"
QEMU="${QEMU:-qemu-system-aarch64}"
command -v "$QEMU" >/dev/null 2>&1 || { echo "error: qemu-system-aarch64 not found" >&2; exit 1; }

# edk2-aarch64 UEFI firmware: Windows (msys2/qemu) и Linux (CI/дистры)
FW=""
for cand in "$MSYS_UCRT/../share/qemu/edk2-aarch64-code.fd" \
            "$ROOT/tools/msys64/ucrt64/share/qemu/edk2-aarch64-code.fd" \
            "$ROOT/../tools/msys64/ucrt64/share/qemu/edk2-aarch64-code.fd" \
            "/usr/share/qemu/edk2-aarch64-code.fd" \
            "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd" \
            "/usr/share/AAVMF/AAVMF_CODE.fd"; do
    [[ -f "$cand" ]] && { FW="$cand"; break; }
done
[[ -n "$FW" ]] || { echo "error: edk2-aarch64 firmware not found" >&2; exit 1; }
command -v cygpath >/dev/null 2>&1 && FW_WIN="$(cygpath -m "$FW")" || FW_WIN="$FW"

# Limine BOOTAA64.EFI: берём из limine/, иначе качаем (тот же релиз, что x86)
if [[ ! -f "$LIMINE_DIR/BOOTAA64.EFI" ]]; then
    echo "[a64-run] downloading Limine v12.6.0 (BOOTAA64.EFI) -> $LIMINE_DIR"
    mkdir -p "$LIMINE_DIR"
    TMPDL="$BUILD_DIR/limine-dl.tar.gz"
    curl -fsL "https://github.com/Limine-Bootloader/Limine/releases/download/v12.6.0/limine-binary.tar.gz" -o "$TMPDL" \
        || wget -qO "$TMPDL" "https://github.com/Limine-Bootloader/Limine/releases/download/v12.6.0/limine-binary.tar.gz"
    tar -xzf "$TMPDL" -C "$LIMINE_DIR" --strip-components=1 BOOTAA64.EFI 2>/dev/null \
        || { tar -xzf "$TMPDL" -C "$LIMINE_DIR" --strip-components=1; find "$LIMINE_DIR" -name "BOOTAA64.EFI" -exec mv {} "$LIMINE_DIR/" \; ; }
    rm -f "$TMPDL"
fi
[[ -f "$LIMINE_DIR/BOOTAA64.EFI" ]] || { echo "error: BOOTAA64.EFI not found" >&2; exit 1; }

# --- initrd (как на x86) ---
PY=""
for cand in python python3 py; do
    if command -v "$cand" >/dev/null 2>&1 && [[ "$(command -v "$cand")" != /c/Users/*/AppData/Local/Microsoft/WindowsApps/* ]]; then
        PY="$cand"; break
    fi
done
PY="${PY:-python3}"
INITRD="$BUILD_DIR/initrd.img"
"$PY" "$ROOT/scripts/mkinitrd.py" "$INITRD" "$ROOT"

# --- limine.conf: как x86-версия + module_path ---
sed '/kernel_path:/a\    module_path: boot():/boot/initrd.img' \
    "$KERNEL_DIR/limine.cfg" > "$BUILD_DIR/limine.conf"

# --- ESP: raw FAT-образ через mtools ---
SECTS=65536   # 32 MiB
mformat -i "$ESP_IMG" -C -T "$SECTS" -v KENGAOS
mmd     -i "$ESP_IMG" ::/EFI ::/EFI/BOOT ::/boot
mcopy   -i "$ESP_IMG" "$LIMINE_DIR/BOOTAA64.EFI" ::/EFI/BOOT/BOOTAA64.EFI
mcopy   -i "$ESP_IMG" "$KERNEL_ELF" ::/boot/kengaos.elf
mcopy   -i "$ESP_IMG" "$BUILD_DIR/limine.conf" ::/boot/limine.conf
mcopy   -i "$ESP_IMG" "$INITRD" ::/boot/initrd.img
echo "ESP: $ESP_IMG ($(stat -c%s "$ESP_IMG") bytes)"

QEMU_ARGS=(-M virt,highmem-ecam=off -cpu cortex-a72 -m 512M
           -bios "$FW_WIN"
           -drive "file=$ESP_IMG,format=raw,if=virtio"
           -device ramfb \
           -device qemu-xhci -device usb-tablet
           -no-reboot)

MODE="${1:-}"
if [[ "$MODE" == "--headless" ]]; then
    UART_LOG="$BUILD_DIR/uart-a64.log"
    : > "$UART_LOG"
    command -v cygpath >/dev/null 2>&1 && WIN_UART="$(cygpath -m "$UART_LOG")" || WIN_UART="$UART_LOG"
    echo "[a64-run] headless smoke: 60s boot (CI-раннеры медленнее), UART -> $UART_LOG"
    timeout 60 "$QEMU" "${QEMU_ARGS[@]}" -display none -serial "file:$WIN_UART" || true
    echo "--- UART output ($UART_LOG) ---"
    cat "$UART_LOG"
    echo "--------------------------------"
    ok=1
    grep -Eq "KengaOS.*booting"  "$UART_LOG" || { echo "ERROR: BOOT marker missing" >&2; ok=0; }
    grep -q  "Hello from Kenga kernel" "$UART_LOG" || { echo "ERROR: UART marker missing" >&2; ok=0; }
    grep -q  "FB READY"          "$UART_LOG" || { echo "ERROR: FB READY marker missing" >&2; ok=0; }
    grep -q  "INTR READY"        "$UART_LOG" || { echo "ERROR: INTR READY marker missing" >&2; ok=0; }
    grep -q  "BRK CAUGHT"        "$UART_LOG" || { echo "ERROR: BRK (vectors) marker missing" >&2; ok=0; }
    grep -q  "MEM READY"         "$UART_LOG" || { echo "ERROR: MEM READY marker missing" >&2; ok=0; }
    grep -q  "PROC READY"        "$UART_LOG" || { echo "ERROR: PROC READY marker missing" >&2; ok=0; }
    [[ "$ok" == 1 ]] && echo "[a64-run] SMOKE OK" || { echo "[a64-run] SMOKE FAILED" >&2; exit 1; }
else
    echo "[a64-run] booting QEMU virt (окно с десктопом; шелл — в этом терминале)"
    exec "$QEMU" "${QEMU_ARGS[@]}" -serial stdio
fi

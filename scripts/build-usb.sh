#!/usr/bin/env bash
# build-usb.sh — загрузочный образ ДИСКА для x86_64 (запись на USB-флешку).
#
# Зачем: до сих пор x86 поставлялся только ISO (CD). Флешка — обычный носитель для
# ноутбука, и грузится она иначе: прошивка читает EFI-раздел прямо с накопителя.
# Здесь собирается FAT-образ с BOOTX64.EFI (Limine), ядром и initrd:
#   dd if=build/kengaos-usb.img of=/dev/sdX bs=4M status=progress conv=fsync
set -u
cd "$(dirname "$0")/.." || exit 1
export PATH="/ucrt64/bin:/usr/bin:/mingw64/bin:/clang64/bin:$PATH"

BUILD=build
ISOBOOT="$BUILD/iso_root/boot"
IMG="$BUILD/kengaos-usb.img"
ELF="$ISOBOOT/kengaos.elf"
INITRD="$ISOBOOT/initrd.img"
CONF="$ISOBOOT/limine.conf"
LIM="limine/BOOTX64.EFI"

[ -f "$ELF" ] || { echo "ERROR: $ELF не найден — сначала bash build/build-x86.sh" >&2; exit 1; }
NEWEST=$(ls -t kernel/*.kenga kernel/*.c 2>/dev/null | head -1)
if [ -n "$NEWEST" ] && [ "$NEWEST" -nt "$ELF" ]; then
    echo "ERROR: ядро УСТАРЕЛО ($NEWEST новее $ELF) — пересоберите" >&2; exit 1
fi
[ -f "$INITRD" ] || { echo "ERROR: initrd не найден" >&2; exit 1; }
[ -f "$LIM" ] || { echo "ERROR: $LIM не найден (пакет Limine)" >&2; exit 1; }
[ -f "$CONF" ] || { echo "ERROR: limine.conf не найден" >&2; exit 1; }

SECTS=131072   # 64 МиБ
rm -f "$IMG"
mformat -i "$IMG" -C -T "$SECTS" -v KENGAOS
mmd -i "$IMG" ::/EFI ::/EFI/BOOT ::/boot
mcopy -i "$IMG" "$LIM" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$IMG" "$ELF" ::/boot/kengaos.elf
mcopy -i "$IMG" "$INITRD" ::/boot/initrd.img
mcopy -i "$IMG" "$CONF" ::/boot/limine.conf

echo "OK: $IMG ($(stat -c%s "$IMG") байт)"
mdir -i "$IMG" ::/EFI/BOOT ::/boot 2>/dev/null | sed 's/^/  /'

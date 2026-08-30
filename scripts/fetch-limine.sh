#!/bin/bash
# KengaOS — скачать и собрать Limine bootloader из исходников.
# Нужен: clang, lld, nasm, mtools, xorriso.
#
# На Debian/Ubuntu/WSL:
#   sudo apt install clang lld nasm mtools xorriso
set -e

LIMINE_VERSION="v12.6.0"
LIMINE_DIR="limine"
ARCH=$(uname -m)

if [ -f "$LIMINE_DIR/bin/limine" ] && [ -f "$LIMINE_DIR/limine-bios.sys" ]; then
    echo "✓ Limine уже установлен в $LIMINE_DIR/"
    exit 0
fi

echo "→ Скачиваю Limine $LIMINE_VERSION..."

URL="https://github.com/limine-bootloader/limine/releases/download/${LIMINE_VERSION}/limine-${LIMINE_VERSION#v}.tar.xz"
TMP_FILE="/tmp/limine-${LIMINE_VERSION}.tar.xz"

curl -L -o "$TMP_FILE" "$URL" || {
    echo "✗ Не удалось скачать Limine с $URL"
    exit 1
}

rm -rf "$LIMINE_DIR" /tmp/limine-build
mkdir -p /tmp/limine-build
tar -xf "$TMP_FILE" -C /tmp/limine-build --strip-components=1
rm "$TMP_FILE"

# Проверить зависимости
echo "→ Проверяю зависимости..."
MISSING=""
for tool in clang nasm mtools xorriso; do
    if ! command -v $tool > /dev/null 2>&1; then
        MISSING="$MISSING $tool"
    fi
done
if [ -n "$MISSING" ]; then
    echo "✗ Отсутствуют инструменты:$MISSING"
    echo "  Установите их (Debian/Ubuntu/WSL):"
    echo "    sudo apt install$MISSING"
    rm -rf /tmp/limine-build
    exit 1
fi

echo "→ Конфигурирую Limine..."
cd /tmp/limine-build
CC_FOR_TARGET=clang ./configure 2>&1 | tail -3

echo "→ Собираю Limine (это может занять ~30 сек)..."
make limine-bios 2>&1 | tail -3
make limine-uefi-cd 2>&1 | tail -3

# Копируем нужные файлы
DEST="$(pwd -P)/$LIMINE_DIR"
mkdir -p "$DEST/bin"
cp /tmp/limine-build/bin/limine "$DEST/bin/"
# Нужные файлы для ISO
cp /tmp/limine-build/limine-bios.sys "$DEST/" 2>/dev/null || true
cp /tmp/limine-build/limine-bios-cd.bin "$DEST/" 2>/dev/null || true
cp /tmp/limine-build/limine-uefi-cd.bin "$DEST/" 2>/dev/null || true
cp /tmp/limine-build/BOOTX64.EFI "$DEST/" 2>/dev/null || true
cp /tmp/limine-build/BOOTIA32.EFI "$DEST/" 2>/dev/null || true

cd "$DEST/.."
rm -rf /tmp/limine-build

echo "✓ Limine $LIMINE_VERSION собран в $LIMINE_DIR/"
ls "$DEST/"
ls "$DEST/bin/"

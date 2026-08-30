#!/bin/bash
# KengaOS — быстрая сборка и запуск в QEMU.
set -e
cd "$(dirname "$0")/.."

echo "=== KengaOS: сборка ==="
make iso

echo ""
echo "=== KengaOS: запуск в QEMU ==="
make run-iso

#!/usr/bin/env python3
"""mkbootimg.py — упаковщик/распаковщик Android boot image (header v0) для KengaOS.

Формат: https://source.android.com/docs/core/bootloader/boot-image-header
Использование:
  python3 mkbootimg.py pack  --kernel IMAGE --dtb DTB --ramdisk RD --out BOOT.IMG
                             [--pagesize 4096] [--base 0x40000000]
                             [--kernel_offset 0x8000] [--ramdisk_offset 0x1000000]
                             [--tags_offset 0x100] [--cmdline "..."] [--board NAME]
  python3 mkbootimg.py unpack --image BOOT.IMG --outdir DIR   # проверка round-trip

KengaOS: kernel = gzip'd arm64 Image, DTB (sm6125) дописывается после ядра
(qualcomm-style: ABL ищет DTB-магик после сжатого ядра).
"""
import argparse
import os
import struct
import sys
import zlib

MAGIC = b"ANDROID!"
PAGE_DEFAULT = 4096
HEADER_SIZE = 2048  # для page_size >= 2048 заголовок занимает одну страницу

FIELDS = [
    ("magic", "8s"),
    ("kernel_size", "I"), ("kernel_addr", "I"),
    ("ramdisk_size", "I"), ("ramdisk_addr", "I"),
    ("second_size", "I"), ("second_addr", "I"),
    ("tags_addr", "I"), ("page_size", "I"),
    ("header_version", "I"), ("os_version", "I"),
    ("name", "16s"),
    ("cmdline", "512s"),
    ("id", "32s"),
    ("extra_cmdline", "1024s"),
]
HEADER_FMT = "<" + "".join(f for _, f in FIELDS)
HEADER_PACKED = struct.calcsize(HEADER_FMT)
assert HEADER_PACKED <= 2048, HEADER_PACKED


def pad(data: bytes, page: int) -> bytes:
    n = (len(data) + page - 1) // page * page
    return data + b"\x00" * (n - len(data))


def pack(a) -> bytes:
    page = a.pagesize
    # ядро: gzip-сжатие, если ещё не сжато (gzipmagic 1f 8b)
    kernel = open(a.kernel, "rb").read()
    if not kernel.startswith(b"\x1f\x8b"):
        kernel = zlib.compress(kernel, 9)
    dtb = open(a.dtb, "rb").read() if a.dtb else b""
    # qualcomm-style: DTB дописывается к ядру (ABL ищет dts magic 0xd00dfeed)
    if dtb:
        kernel += b"\x00" * ((4 - len(kernel) % 4) % 4) + dtb
    ramdisk = open(a.ramdisk, "rb").read() if a.ramdisk else b""

    hdr = struct.pack(
        HEADER_FMT,
        MAGIC,
        len(kernel), (a.base + a.kernel_offset) & 0xFFFFFFFF,
        len(ramdisk), (a.base + a.ramdisk_offset) & 0xFFFFFFFF,
        0, 0,                                  # second
        (a.base + a.tags_offset) & 0xFFFFFFFF,
        page, 0, 0,                            # header_version, os_version
        a.board.encode()[:16].ljust(16, b"\x00"),
        a.cmdline.encode()[:512].ljust(512, b"\x00"),
        b"\x00" * 32,                          # id (заполняется при прошивке)
        b"\x00" * 1024,                        # extra_cmdline
    )
    out = pad(hdr, page)
    out += pad(kernel, page)
    out += pad(ramdisk, page)
    return out


def unpack(a) -> int:
    data = open(a.image, "rb").read()
    if data[:8] != MAGIC:
        print("error: не boot image (magic)", file=sys.stderr)
        return 1
    vals = struct.unpack(HEADER_FMT, data[:HEADER_PACKED])
    hdr = dict(zip([n for n, _ in FIELDS], vals))
    page = hdr["page_size"]
    os.makedirs(a.outdir, exist_ok=True)
    off = page
    for part in ("kernel", "ramdisk", "second"):
        sz = hdr[f"{part}_size"]
        if sz:
            blob = data[off:off + sz]
            name = os.path.join(a.outdir, part + ".img")
            open(name, "wb").write(blob)
            print(f"{part}: {sz} bytes -> {name}")
        off += (sz + page - 1) // page * page
    print("cmdline:", hdr["cmdline"].rstrip(bytes([0])).decode(errors="replace"))
    print(f"page={page} kernel_addr={hdr['kernel_addr']:#x} tags_addr={hdr['tags_addr']:#x}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    pp = sub.add_parser("pack")
    pp.add_argument("--kernel", required=True)
    pp.add_argument("--dtb")
    pp.add_argument("--ramdisk")
    pp.add_argument("--out", required=True)
    pp.add_argument("--pagesize", type=lambda x: int(x, 0), default=PAGE_DEFAULT)
    pp.add_argument("--base", type=lambda x: int(x, 0), default=0x40000000)
    pp.add_argument("--kernel_offset", type=lambda x: int(x, 0), default=0x8000)
    pp.add_argument("--ramdisk_offset", type=lambda x: int(x, 0), default=0x1000000)
    pp.add_argument("--tags_offset", type=lambda x: int(x, 0), default=0x100)
    pp.add_argument("--cmdline", default="console=ttyMSM0,115200n8 earlycon=msm_geni_serial,0x4a90000")
    pp.add_argument("--board", default="KENGAOS")

    up = sub.add_parser("unpack")
    up.add_argument("--image", required=True)
    up.add_argument("--outdir", required=True)

    a = ap.parse_args()
    if a.cmd == "pack":
        blob = pack(a)
        open(a.out, "wb").write(blob)
        print(f"{a.out}: {len(blob)} bytes, page={a.pagesize}")
    else:
        sys.exit(unpack(a))


if __name__ == "__main__":
    main()

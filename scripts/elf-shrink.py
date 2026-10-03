#!/usr/bin/env python3
"""elf-shrink.py — обрезает ELF до реально загружаемых байт (sstrip-lite).

Ядро (kernel/kf_user.c: elf_load) использует только ELF-заголовок и program
headers; таблица секций, .symtab и прочее в образ не попадают, но занимают
место в файле. Для ring-3 приложений из KengaFS это существенно: файл
целиком читается в 4 КиБ буфер ядра (st.dat).

Что делает:
  - считает max(p_offset + p_filesz) по всем PT_LOAD;
  - обрезает файл до этой длины;
  - обнуляет e_shoff / e_shnum / e_shstrndx.

Не трогает: e_entry, program headers, содержимое сегментов.
"""
import struct
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: elf-shrink.py <in.elf> <out.elf>", file=sys.stderr)
        return 2
    data = bytearray(open(sys.argv[1], "rb").read())
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        print("elf-shrink: not a 64-bit little-endian ELF", file=sys.stderr)
        return 1

    e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x36)[0]
    e_phnum = struct.unpack_from("<H", data, 0x38)[0]
    need = e_phoff + e_phentsize * e_phnum

    end = need
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if struct.unpack_from("<I", data, off)[0] != 1:      # PT_LOAD
            continue
        p_offset = struct.unpack_from("<Q", data, off + 0x08)[0]
        p_filesz = struct.unpack_from("<Q", data, off + 0x20)[0]
        end = max(end, p_offset + p_filesz)

    if end > len(data):
        print("elf-shrink: segment beyond file end", file=sys.stderr)
        return 1

    out = data[:end]
    struct.pack_into("<Q", out, 0x28, 0)   # e_shoff
    struct.pack_into("<H", out, 0x3C, 0)   # e_shnum
    struct.pack_into("<H", out, 0x3E, 0)   # e_shstrndx
    open(sys.argv[2], "wb").write(out)
    print("elf-shrink: %d -> %d bytes" % (len(data), len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""mkinitrd.py — build an initrd for KengaOS from real host/git data.

Format (little-endian):
  u32 file_count
  per file:
    u32 name_len, name bytes, u32 size, data bytes
Output is written to the given path.

Each file is "virtual" content that the OS shell can read with ls/cat.
"""
import subprocess, sys, platform, os

def sh(cmd):
    try:
        return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=10, errors="replace").stdout.strip()
    except Exception:
        return "n/a"

def collect(root):
    """Return {name: bytes} for the initrd entries."""
    entries = {}
    entries["version"]  = (sh(f"cd {root} && git describe --tags 2>/dev/null || echo KengaOS-dev") + "\n").encode()
    entries["kernel"]   = "KengaOS — x86_64 kernel written in Kenga, booted by Limine\n".encode()
    entries["cpu"]      = (platform.machine() + " / " + platform.system() + " (build host)\n").encode()
    entries["host"]     = (platform.node() + " " + platform.release() + "\n").encode()
    log = sh(f"cd {root} && git log --oneline -8 2>/dev/null || echo no-git")
    entries["gitlog"]   = (log + "\n").encode()
    # list kernel source files
    kdir = os.path.join(root, "kernel")
    files = ""
    if os.path.isdir(kdir):
        for f in sorted(os.listdir(kdir)):
            if os.path.isfile(os.path.join(kdir, f)):
                files += f + "\n"
    entries["kernel-src"] = files.encode()
    # KengaOS Store: .kpkg v1 манифесты (текстовые, формат см. kernel/kf_pkg.c)
    NL = chr(10)
    entries["pkg-calc.kpkg"] = NL.join([
        "name=Калькулятор", "version=1.0.0",
        "desc=Считает через модель-агента (XOR-MLP)", "entry=calc", ""]).encode()
    entries["pkg-notes.kpkg"] = NL.join([
        "name=Блокнот", "version=0.9.0",
        "desc=Текстовые заметки в агента-окне", "entry=notes", ""]).encode()
    hello = os.path.join(root, "build", "user", "hello.elf")
    if os.path.isfile(hello):
        entries["user-hello.elf"] = open(hello, "rb").read()
    entries["pkg-agents.kpkg"] = NL.join([
        "name=Агенты", "version=1.1.0",
        "desc=Панель IPC-агентов системы", "entry=agents", ""]).encode()
    return entries

def main():
    out = sys.argv[1]
    root = sys.argv[2] if len(sys.argv) > 2 else "."
    entries = collect(root)
    data = bytearray()
    data += len(entries).to_bytes(4, "little")
    for name, blob in sorted(entries.items()):
        nb = name.encode()
        data += len(nb).to_bytes(4, "little")
        data += nb
        data += len(blob).to_bytes(4, "little")
        data += blob
    with open(out, "wb") as f:
        f.write(bytes(data))
    print(f"initrd: {len(entries)} files, {len(data)} bytes -> {out}")

if __name__ == "__main__":
    main()

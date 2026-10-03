#!/usr/bin/env python3
"""patch-kenga-user.py — готовит вывод `kenga emit-c --freestanding` к ring 3.

emit-c делает kenga_print*() заглушками (no-op), потому что в ядре печать
идёт через kf_* FFI. Пользовательскому приложению нужен syscall write, поэтому:
  - в начало файла добавляются объявления k_sys_writeln (из kenga_user_rt.c);
  - заглушки println_str / println_i64 / println_f64 заменяются на int 0x80.

Скрипт намеренно строгий: если ожидаемая строка не найдена — падает, чтобы
смена формата emit-c не прошла незамеченной."""
import sys

def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch-kenga-user.py <generated.c>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    with open(path, encoding="utf-8") as fh:
        src = fh.read()

    header = ("#include <math.h>\n"
              "\n/* --- KengaOS user-mode (ring 3): syscall-хуки из kenga_user_rt.c --- */\n"
              "extern long k_sys_writeln(const char*);\n")
    if "#include <math.h>\n" not in src:
        print("patch: '#include <math.h>' not found", file=sys.stderr)
        return 1
    if "k_sys_writeln" not in src:
        src = src.replace("#include <math.h>\n", header, 1)

    subs = [
        ("static void kenga_println_str(const char * /*s*/) { }",
         "static void kenga_println_str(const char *s) { k_sys_writeln(s); }"),
        ("static void kenga_println_i64(int64_t /*v*/) { }",
         "static void kenga_println_i64(int64_t v) {\n"
         "  char b[24]; int n = 0; unsigned long long u;\n"
         "  if (v < 0) { b[n++] = '-'; u = (unsigned long long)(-(v + 1)) + 1ull; } else { u = (unsigned long long)v; }\n"
         "  char t[24]; int k = 0;\n"
         "  do { t[k++] = (char)('0' + (u % 10)); u /= 10; } while (u);\n"
         "  while (k) b[n++] = t[--k];\n"
         "  b[n] = 0;\n"
         "  k_sys_writeln(b);\n"
         "}"),
        ("static void kenga_println_f64(double /*v*/) { }",
         "static void kenga_println_f64(double v) { kenga_println_i64((int64_t)v); }"),
    ]
    for old, new in subs:
        if old not in src:
            print("patch: expected stub not found: %s" % old, file=sys.stderr)
            return 1
        src = src.replace(old, new, 1)

    with open(path, "w", encoding="utf-8") as fh:
        fh.write(src)
    print("patched: %s" % path)
    return 0

if __name__ == "__main__":
    sys.exit(main())

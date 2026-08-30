# Генератор AA-шрифтового атласа для UI KengaOS.
# Запуск:  python tools/genfont.py
# Выход:   fonts/ui_font.c + fonts/ui_font.h
# Атласы: ui13 (Segoe UI 13), uib13 (Segoe UI Bold 13),
#         mono12 (Consolas 12), uib30 (Segoe UI Bold 30 — часы/локскрин).
import os
from PIL import Image, ImageFont, ImageDraw

OUT_C = os.path.join(os.path.dirname(__file__), "..", "fonts", "ui_font.c")
OUT_H = os.path.join(os.path.dirname(__file__), "..", "fonts", "ui_font.h")

# codepoint-набор: ASCII + кириллица + пара символов
def charset():
    cps = list(range(0x20, 0x7F))                          # ASCII
    cps += [0x401] + list(range(0x410, 0x450)) + [0x451]   # Ё А-я ё
    cps += [0xB7, 0x2116, 0x2014, 0x2192]                   # · № — →
    cps += [0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x2592, 0x2588]  # ═║╔╗╚╝▓█-серия
    return cps

FONTS = [
    ("ui",    "C:/Windows/Fonts/segoeui.ttf",  13),
    ("uib",   "C:/Windows/Fonts/segoeuib.ttf", 13),
    ("mono",  "C:/Windows/Fonts/consola.ttf",  12),
    ("uib30", "C:/Windows/Fonts/segoeuib.ttf", 30),
]

def build(name, path, px):
    font = ImageFont.truetype(path, px)
    glyphs = []  # (cp, img, ox, oy, advance)
    for cp in charset():
        ch = chr(cp)
        try:
            bbox = font.getbbox(ch)
            w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
            if w <= 0 or h <= 0 or w > 80 or h > 80:
                continue
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-bbox[0], -bbox[1]), ch, font=font, fill=255)
            adv = int(round(font.getlength(ch))) or w
            glyphs.append((cp, img, bbox[0], bbox[1], adv))
        except Exception:
            continue
    # shelf packing
    atlas_w = 512
    rows, places = [], []  # rows: [y, x_next, h]
    for cp, img, ox, oy, adv in glyphs:
        gw, gh = img.size
        placed = False
        for row in rows:
            if row[2] >= gh and row[1] + gw + 1 <= atlas_w:
                places.append((cp, row[1], row[0], gw, gh, ox, oy, adv))
                row[1] += gw + 1
                placed = True
                break
        if not placed:
            y = rows[-1][0] + rows[-1][2] + 1 if rows else 0
            rows.append([y, gw + 1, gh])
            places.append((cp, 0, y, gw, gh, ox, oy, adv))
    atlas_h = rows[-1][0] + rows[-1][2] + 1 if rows else 1
    atlas = Image.new("L", (atlas_w, atlas_h), 0)
    for cp, x, y, gw, gh, ox, oy, adv in places:
        img = next(i for c, i, *_ in glyphs if c == cp)
        atlas.paste(img, (x, y))
    return places, atlas

def emit():
    hdr = ["/* Сгенерировано tools/genfont.py — не править руками.",
           "   AA-атласы UI-шрифтов KengaOS (Segoe UI / Consolas). */",
           "#ifndef KENGA_UI_FONT_H", "#define KENGA_UI_FONT_H", "",
           "#include \"../kernel/lib/types.h\"", "",
           "typedef struct { u16 u, v, w, h; i16 ox, oy; u16 adv; } kglyph_t;",
           "typedef struct { const u8 *alpha; u16 w, h; const kglyph_t *g;",
           "                 const u16 *cps; u16 n; } kfont_t;", "",
           "extern const kfont_t kf_ui, kf_uib, kf_mono, kf_uib30;", "",
           "/* поиск глифа по codepoint; NULL если нет */",
           "const kglyph_t *kf_lookup(const kfont_t *f, u32 cp);", "",
           "#endif"]
    open(OUT_H, "w", encoding="utf-8").write("\n".join(hdr) + "\n")

    c = ["/* Сгенерировано tools/genfont.py */",
         "#include \"ui_font.h\"", ""]
    for name, path, px in FONTS:
        places, atlas = build(name, path, px)
        px_data = list(atlas.getdata())
        c.append(f"static const u8 atlas_{name}[{len(px_data)}] = {{")
        for i in range(0, len(px_data), 24):
            c.append("  " + ",".join(str(b) for b in px_data[i:i+24]) + ",")
        c.append("};")
        c.append(f"static const kglyph_t glyphs_{name}[{len(places)}] = {{")
        for cp, x, y, gw, gh, ox, oy, adv in places:
            c.append(f"  {{{x},{y},{gw},{gh},{ox},{oy},{adv}}},")
        c.append("};")
        cps = [p[0] for p in places]
        c.append(f"static const u16 cps_{name}[{len(cps)}] = "
                 f"{{{','.join(str(x) for x in cps)}}};")
        c.append(f"const kfont_t kf_{name} = {{ atlas_{name}, {atlas.width}, "
                 f"{atlas.height}, glyphs_{name}, cps_{name}, {len(places)} }};")
        c.append("")
    c.append("""
const kglyph_t *kf_lookup(const kfont_t *f, u32 cp) {
    if (cp > 0xFFFF) return 0;
    for (u16 i = 0; i < f->n; i++)
        if (f->cps[i] == (u16)cp) return &f->g[i];
    return 0;
}
""")
    open(OUT_C, "w", encoding="utf-8").write("\n".join(c) + "\n")
    print(f"OK -> {OUT_C}")

if __name__ == "__main__":
    emit()

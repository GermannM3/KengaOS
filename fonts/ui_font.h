/* Сгенерировано tools/genfont.py — не править руками.
   AA-атласы UI-шрифтов KengaOS (Segoe UI / Consolas). */
#ifndef KENGA_UI_FONT_H
#define KENGA_UI_FONT_H

#include "../kernel/lib/types.h"

typedef struct { u16 u, v, w, h; i16 ox, oy; u16 adv; } kglyph_t;
typedef struct { const u8 *alpha; u16 w, h; const kglyph_t *g;
                 const u16 *cps; u16 n; } kfont_t;

extern const kfont_t kf_ui, kf_uib, kf_mono, kf_uib30;

/* поиск глифа по codepoint; NULL если нет */
const kglyph_t *kf_lookup(const kfont_t *f, u32 cp);

#endif

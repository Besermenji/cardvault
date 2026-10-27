// Reusable drawing helpers on the off-screen canvas.
#pragma once

#include <M5GFX.h>

#include <vector>

#include "crypto/secure.h"
#include "ui/textwrap.h"
#include "ui/theme.h"
#include "vault/model.h"

namespace cv {
namespace ui {

using Canvas = M5Canvas;

void clear(Canvas& c);
void header(Canvas& c, const char* title, uint16_t accent = color::accent);
void footer(Canvas& c, const char* hints);
void type_badge(Canvas& c, int x, int y, EntryType t);
uint16_t type_color(EntryType t);
void list_row(Canvas& c, int y, int h, bool selected);
void progress(Canvas& c, int x, int y, int w, int h, float frac, uint16_t fg);
// Centered dialog box with title and wrapped message.
void dialog(Canvas& c, const char* title, const char* msg, uint16_t accent);
// Draws a scroll indicator on the right edge.
void scrollbar(Canvas& c, int first, int visible, int total);
// Draws `len` mask glyphs.
void mask(Canvas& c, int x, int y, size_t len, int max_cols);
void text_small(Canvas& c, int x, int y, const char* s, uint16_t col = color::dim);
void text_mono(Canvas& c, int x, int y, const char* s, uint16_t col = color::text);

#if defined(CV_SCREENSHOT) || defined(CV_TEST_HOOKS)
// Screenshots (debug and e2e builds only): row y of the canvas as RGB565, 4 hex
// digits per pixel. `out` holds c.width() * 4 + 1 characters.
void row_hex(Canvas& c, int y, char* out);
#endif

}  // namespace ui
}  // namespace cv

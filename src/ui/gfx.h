// Reusable drawing helpers on the off-screen canvas.
#pragma once

#include <M5GFX.h>

#include <vector>

#include "crypto/secure.h"
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
// Word-wraps text into lines of at most `cols` characters. Each line is a
// (start, length) pair into `text`; explicit '\n' always breaks.
struct Line {
    size_t start;
    size_t len;
};
std::vector<Line> wrap(const char* text, size_t n, int cols, bool hard = false);
// Draws `len` mask glyphs.
void mask(Canvas& c, int x, int y, size_t len, int max_cols);
void text_small(Canvas& c, int x, int y, const char* s, uint16_t col = color::dim);
void text_mono(Canvas& c, int x, int y, const char* s, uint16_t col = color::text);
// Truncates with "..." so it fits in `cols` monospace columns.
SString fit(const SString& s, int cols);

}  // namespace ui
}  // namespace cv

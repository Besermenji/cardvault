// Text layout helpers shared by the editor and viewers (hardware-independent).
#pragma once

#include <cstddef>
#include <vector>

#include "crypto/secure.h"

namespace cv {
namespace ui {

// A visual line: (start, length) into the text.
struct Line {
    size_t start;
    size_t len;
};

// Wraps text into lines of at most `cols` characters. '\n' always breaks.
// Soft mode breaks at the last space when possible; hard mode breaks exactly
// at `cols` (used by the editor so cursor columns map 1:1 to characters).
std::vector<Line> wrap(const char* text, size_t n, int cols, bool hard = false);

// Visual (line, column) of cursor index `cur` within `lines` (hard wrap).
void cursor_locate(const std::vector<Line>& lines, size_t cur, int cols, int& line, size_t& col);

// Cursor index after moving `delta` visual lines, keeping the column when
// possible. Returns `cur` unchanged at the first/last line.
size_t cursor_move_line(const std::vector<Line>& lines, size_t cur, int cols, int delta);

// Truncates with "..." so it fits in `cols` characters.
SString fit(const SString& s, int cols);

}  // namespace ui
}  // namespace cv

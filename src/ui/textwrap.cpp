#include "ui/textwrap.h"

#include <cstdint>

namespace cv {
namespace ui {

std::vector<Line> wrap(const char* text, size_t n, int cols, bool hard) {
    std::vector<Line> lines;
    if (cols < 1) cols = 1;
    size_t start = 0;
    while (start <= n) {
        size_t end = start;
        size_t last_space = SIZE_MAX;
        while (end < n && text[end] != '\n' && int(end - start) < cols) {
            if (text[end] == ' ') last_space = end;
            end++;
        }
        if (end < n && text[end] == ' ' && !hard) {
            // The line is exactly full and a space follows: break there.
            lines.push_back({start, end - start});
            start = end + 1;
            continue;
        }
        if (end < n && text[end] != '\n' && int(end - start) >= cols && !hard &&
            last_space != SIZE_MAX && last_space > start) {
            lines.push_back({start, last_space - start});
            start = last_space + 1;
            continue;
        }
        lines.push_back({start, end - start});
        if (end >= n) break;
        start = (text[end] == '\n') ? end + 1 : end;
        if (start == n && text[n - 1] == '\n') {
            lines.push_back({n, 0});
            break;
        }
    }
    if (lines.empty()) lines.push_back({0, 0});
    return lines;
}

void cursor_locate(const std::vector<Line>& lines, size_t cur, int cols, int& line, size_t& col) {
    for (size_t i = 0; i < lines.size(); i++) {
        const Line& l = lines[i];
        bool last = i + 1 == lines.size();
        bool inside = cur >= l.start && cur < l.start + l.len;
        bool at_end = cur == l.start + l.len && (last || lines[i + 1].start > cur);
        if (inside || at_end) {
            line = int(i);
            col = cur - l.start;
            if (col >= size_t(cols) && !last) {  // exactly full line: cursor on next
                line = int(i) + 1;
                col = 0;
            }
            return;
        }
    }
    line = int(lines.size()) - 1;
    col = lines.back().len;
}

size_t cursor_move_line(const std::vector<Line>& lines, size_t cur, int cols, int delta) {
    int line;
    size_t col;
    cursor_locate(lines, cur, cols, line, col);
    int target = line + delta;
    if (target < 0 || target >= int(lines.size())) return cur;
    const Line& l = lines[target];
    return l.start + (col < l.len ? col : l.len);
}

SString fit(const SString& s, int cols) {
    if (int(s.size()) <= cols) return s;
    if (cols <= 3) return s.substr(0, cols);
    return s.substr(0, cols - 3) + "...";
}

}  // namespace ui
}  // namespace cv

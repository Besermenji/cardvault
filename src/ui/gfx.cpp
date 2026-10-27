#include "ui/gfx.h"

#include <cstdio>
#include <cstring>

#include "platform/device.h"

namespace cv {
namespace ui {

void clear(Canvas& c) { c.fillScreen(color::bg); }

void header(Canvas& c, const char* title, uint16_t accent) {
    c.fillRect(0, 0, kW, kHeaderH, color::panel);
    c.fillRect(0, kHeaderH - 2, kW, 2, accent);
    c.setFont(CV_FONT_TITLE);
    c.setTextColor(color::text);
    c.setTextDatum(middle_left);
    c.drawString(title, 5, kHeaderH / 2 - 1);

    // Right side: clock (if set) and battery.
    static uint32_t last_read = 0;
    static int battery = -1;
    if (battery < 0 || millis() - last_read > 10000) {
        battery = device::battery_percent();
        last_read = millis();
    }
    c.setFont(CV_FONT_SMALL);
    c.setTextDatum(middle_right);
    int x = kW - 4;
    int bw = 18, bh = 9, by = 4;
    c.drawRect(x - bw, by, bw, bh, color::dim);
    c.fillRect(x, by + 2, 2, bh - 4, color::dim);
    int fill = (battery * (bw - 4)) / 100;
    uint16_t bc = battery < 20 ? color::bad : battery < 50 ? color::warn : color::good;
    if (fill > 0) c.fillRect(x - bw + 2, by + 2, fill, bh - 4, bc);
    x -= bw + 5;
    if (device::time_valid()) {
        time_t t = time(nullptr);
        struct tm tm;
        gmtime_r(&t, &tm);
        char buf[8];
        snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
        c.setTextColor(color::dim);
        c.drawString(buf, x, by + 5);
    }
    c.setTextDatum(top_left);
}

void footer(Canvas& c, const char* hints) {
    int y = kH - kFooterH;
    c.fillRect(0, y, kW, kFooterH, color::panel);
    c.setFont(CV_FONT_SMALL);
    c.setTextColor(color::dim);
    c.setTextDatum(middle_left);
    c.drawString(hints, 4, y + kFooterH / 2);
    c.setTextDatum(top_left);
}

uint16_t type_color(EntryType t) {
    switch (t) {
        case EntryType::Mnemonic: return color::accent;
        case EntryType::Password: return color::info;
        case EntryType::Totp: return color::good;
        case EntryType::Note: return color::purple;
    }
    return color::dim;
}

void type_badge(Canvas& c, int x, int y, EntryType t) {
    const char* g = "?";
    switch (t) {
        case EntryType::Mnemonic: g = "S"; break;
        case EntryType::Password: g = "P"; break;
        case EntryType::Totp: g = "2"; break;
        case EntryType::Note: g = "N"; break;
    }
    c.fillRoundRect(x, y, 14, 14, 3, type_color(t));
    c.setFont(CV_FONT_SMALL);
    c.setTextColor(color::bg);
    c.setTextDatum(middle_center);
    c.drawString(g, x + 7, y + 7);
    c.setTextDatum(top_left);
}

void list_row(Canvas& c, int y, int h, bool selected) {
    if (selected) {
        c.fillRoundRect(2, y, kW - 8, h, 3, color::sel);
        c.fillRect(2, y + 2, 2, h - 4, color::accent);
    }
}

void progress(Canvas& c, int x, int y, int w, int h, float frac, uint16_t fg) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    c.fillRoundRect(x, y, w, h, h / 2, color::line);
    int fw = int(w * frac);
    if (fw > 0) c.fillRoundRect(x, y, fw, h, h / 2, fg);
}

void dialog(Canvas& c, const char* title, const char* msg, uint16_t accent) {
    int x = 12, y = 26, w = kW - 24, h = kH - 46;
    c.fillRoundRect(x + 2, y + 2, w, h, 6, color::bg);
    c.fillRoundRect(x, y, w, h, 6, color::panel);
    c.drawRoundRect(x, y, w, h, 6, accent);
    c.setFont(CV_FONT_TITLE);
    c.setTextColor(accent);
    c.setTextDatum(top_center);
    c.drawString(title, kW / 2, y + 6);
    c.setFont(CV_FONT_SMALL);
    c.setTextColor(color::text);
    int cols = (w - 16) / 6;
    auto lines = wrap(msg, strlen(msg), cols);
    int ly = y + 28;
    for (auto& l : lines) {
        if (ly > y + h - 10) break;
        SString s(msg + l.start, l.len);
        c.drawString(s.c_str(), kW / 2, ly);
        ly += 10;
    }
    c.setTextDatum(top_left);
}

void scrollbar(Canvas& c, int first, int visible, int total) {
    if (total <= visible) return;
    int x = kW - 4, y = kBodyY, h = kBodyH;
    c.fillRect(x, y, 2, h, color::line);
    int th = h * visible / total;
    if (th < 6) th = 6;
    int ty = y + (h - th) * first / (total - visible);
    c.fillRect(x, ty, 2, th, color::dim);
}

void mask(Canvas& c, int x, int y, size_t len, int max_cols) {
    size_t n = len < size_t(max_cols) ? len : size_t(max_cols);
    for (size_t i = 0; i < n; i++) c.fillCircle(x + int(i) * kMonoW + 4, y + 8, 2, color::dim);
}

void text_small(Canvas& c, int x, int y, const char* s, uint16_t col) {
    c.setFont(CV_FONT_SMALL);
    c.setTextColor(col);
    c.drawString(s, x, y);
}

void text_mono(Canvas& c, int x, int y, const char* s, uint16_t col) {
    c.setFont(CV_FONT_MONO);
    c.setTextColor(col);
    c.drawString(s, x, y);
}

#if defined(CV_SCREENSHOT) || defined(CV_TEST_HOOKS)
void row_hex(Canvas& c, int y, char* out) {
    for (int x = 0; x < c.width(); x++) snprintf(out + x * 4, 5, "%04x", unsigned(c.readPixel(x, y)));
}
#endif

}  // namespace ui
}  // namespace cv

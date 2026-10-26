// Colours, fonts and layout constants.
#pragma once

#include <M5GFX.h>

#include "ui/layout.h"

namespace cv {
namespace ui {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace color {
constexpr uint16_t bg = rgb(11, 14, 20);
constexpr uint16_t panel = rgb(24, 29, 40);
constexpr uint16_t sel = rgb(38, 48, 74);
constexpr uint16_t line = rgb(48, 56, 72);
constexpr uint16_t text = rgb(230, 233, 239);
constexpr uint16_t dim = rgb(122, 132, 153);
constexpr uint16_t accent = rgb(247, 147, 26);  // bitcoin orange
constexpr uint16_t good = rgb(63, 185, 80);
constexpr uint16_t bad = rgb(248, 81, 73);
constexpr uint16_t warn = rgb(210, 153, 34);
constexpr uint16_t info = rgb(88, 166, 255);
constexpr uint16_t purple = rgb(188, 140, 255);
}  // namespace color

// Fonts (glyph sizes in ui/layout.h: keep them in sync)
#define CV_FONT_MONO (&fonts::AsciiFont8x16)  // kMonoW x kMonoH, values/input
#define CV_FONT_SMALL (&fonts::Font0)         // kSmallW x kSmallH, hints and labels
#define CV_FONT_TITLE (&fonts::FreeSansBold9pt7b)
#define CV_FONT_BIG (&fonts::Orbitron_Light_32)

}  // namespace ui
}  // namespace cv

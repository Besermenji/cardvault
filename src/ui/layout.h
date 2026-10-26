// Screen geometry and glyph sizes (hardware-independent, so host tests can
// check layouts against them).
#pragma once

namespace cv {
namespace ui {

constexpr int kW = 240;
constexpr int kH = 135;
constexpr int kHeaderH = 18;
constexpr int kFooterH = 11;
constexpr int kBodyY = kHeaderH + 2;
constexpr int kBodyH = kH - kHeaderH - kFooterH - 3;

// Glyph cells of CV_FONT_MONO (AsciiFont8x16) and CV_FONT_SMALL (Font0).
constexpr int kMonoW = 8;
constexpr int kMonoH = 16;
constexpr int kSmallW = 6;
constexpr int kSmallH = 8;

// Password generator output box: 8 px margins, two mono lines or, for
// longer output, three small-font lines.
constexpr int kPwgenCols = (kW - 16) / kMonoW;        // 28
constexpr int kPwgenColsSmall = (kW - 16) / kSmallW;  // 37

}  // namespace ui
}  // namespace cv

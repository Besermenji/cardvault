// Boot splash: a vault-door dial that turns and clicks into place.
#include <M5Cardputer.h>

#include <cmath>

#include "core/flows.h"
#include "ui/app.h"
#include "version.h"

namespace cv {
namespace ui {

namespace {

constexpr int kDialX = 56;
constexpr int kDialY = kH / 2;
constexpr int kDialR = 46;
constexpr int kTextX = 114;
constexpr float kPi = 3.14159265f;

void polar(int cx, int cy, float r, float deg, int& x, int& y) {
    float a = deg * kPi / 180.0f;
    x = cx + int(lroundf(r * cosf(a)));
    y = cy + int(lroundf(r * sinf(a)));
}

}  // namespace

void draw_splash(Canvas& c, float progress, bool settled) {
    clear(c);
    uint16_t lit = settled ? color::accent : color::dim;
    float turn = progress * 90.0f;

    // Door: a rim with tick marks that turns, and four fixed bolts.
    c.fillCircle(kDialX, kDialY, kDialR, color::panel);
    c.drawCircle(kDialX, kDialY, kDialR, color::line);
    c.fillCircle(kDialX, kDialY, kDialR - 10, color::bg);
    c.drawCircle(kDialX, kDialY, kDialR - 10, color::line);
    for (int i = 0; i < 24; i++) {
        int x0, y0, x1, y1;
        float deg = turn + 15.0f * i;
        polar(kDialX, kDialY, kDialR - 8, deg, x0, y0);
        polar(kDialX, kDialY, kDialR - 3, deg, x1, y1);
        c.drawLine(x0, y0, x1, y1, i % 6 == 0 ? color::text : color::dim);
    }
    for (int i = 0; i < 4; i++) {
        int x, y;
        polar(kDialX, kDialY, kDialR + 1, 90.0f * i, x, y);
        c.fillCircle(x, y, 3, lit);
    }

    // Handle: four spokes and a hub.
    for (int i = 0; i < 4; i++) {
        int x0, y0, x1, y1;
        float deg = 45.0f + turn + 90.0f * i;
        polar(kDialX, kDialY, 8, deg, x0, y0);
        polar(kDialX, kDialY, kDialR - 14, deg, x1, y1);
        c.drawWideLine(x0, y0, x1, y1, 2, color::text);
        c.fillCircle(x1, y1, 3, color::text);
    }
    c.fillCircle(kDialX, kDialY, 9, lit);
    c.fillCircle(kDialX, kDialY, 4, color::bg);

    // Name, rule, tagline, version.
    c.setFont(CV_FONT_TITLE);
    c.setTextColor(color::text);
    c.drawString("CardVault", kTextX, kDialY - 30);
    c.fillRect(kTextX, kDialY - 6, 40, 2, color::accent);
    text_small(c, kTextX, kDialY + 4, "Offline encrypted", color::dim);
    text_small(c, kTextX, kDialY + 14, "secrets vault", color::dim);
    char ver[16];
    snprintf(ver, sizeof ver, "v%s", CV_VERSION);
    c.setFont(CV_FONT_SMALL);
    c.setTextColor(color::dim);
    c.setTextDatum(bottom_right);
    c.drawString(ver, kW - 6, kH - 4);
    c.setTextDatum(top_left);
}

namespace {

class SplashScreen : public Screen {
public:
    SplashScreen(uint32_t start_ms, const char* next_name, std::function<ScreenPtr()> next)
        : start_(start_ms), next_name_(next_name), next_(std::move(next)) {}

    const char* name() const override { return "splash"; }
#ifdef CV_TEST_HOOKS
    void debug(std::string& out) const override {
        out = std::string("anim=") + (settled_ ? "1" : "0") + " next=" + next_name_;
    }
#endif

    void on_key(const Key&) override { finish(); }

    void tick(uint32_t now) override {
        // `now` can be older than start_ms (taken before the screen was built).
        int32_t d = int32_t(now - start_);
        uint32_t elapsed = d < 0 ? 0 : uint32_t(d);
        if (splash_done(elapsed, kSplashMs, false)) {
            finish();
            return;
        }
        progress_ = splash_anim(elapsed);
        bool settled = elapsed >= kSplashAnimMs;
        if (!settled || !settled_) invalidate();  // animating, or the one frame that settles
        settled_ = settled;
    }

    void draw(Canvas& c) override { draw_splash(c, progress_, settled_); }

private:
    void finish() {
        if (done_) return;
        done_ = true;
        app().router.reset(next_());
    }

    uint32_t start_;
    const char* next_name_;
    std::function<ScreenPtr()> next_;
    float progress_ = 0;
    bool settled_ = false;
    bool done_ = false;
};

}  // namespace

ScreenPtr make_splash(uint32_t start_ms, const char* next_name, std::function<ScreenPtr()> next) {
    return ScreenPtr(new SplashScreen(start_ms, next_name, std::move(next)));
}

}  // namespace ui
}  // namespace cv

#include "ui/app.h"

#include <M5Cardputer.h>

namespace cv {
namespace ui {

App& app() {
    static App instance;
    return instance;
}

void App::lock_now() {
    lock_pending = false;
    router.clear_now();
    session.close();
    router.reset(make_unlock());
}

bool App::save() {
    VaultError e = session.save(store);
    if (e != VaultError::Ok) {
        toast(vault_error_str(e), color::bad, 3000);
        return false;
    }
    return true;
}

void App::wipe_and_reboot(const char* reason) {
    router.clear_now();
    session.close();
    if (canvas) {
        clear(*canvas);
        dialog(*canvas, "Erasing vault", reason, color::bad);
        canvas->pushSprite(0, 0);
    }
    store.erase();
    guard.reset();
    delay(2500);
    ESP.restart();
    for (;;) {
    }
}

void App::busy(const char* msg) {
    if (!canvas) return;
    Canvas& c = *canvas;
    int w = 180, h = 44, x = (kW - w) / 2, y = (kH - h) / 2;
    c.fillRoundRect(x, y, w, h, 6, color::panel);
    c.drawRoundRect(x, y, w, h, 6, color::accent);
    c.setFont(CV_FONT_MONO);
    c.setTextColor(color::text);
    c.setTextDatum(middle_center);
    c.drawString(msg, kW / 2, kH / 2);
    c.setTextDatum(top_left);
    c.pushSprite(0, 0);
    if (Screen* t = router.top()) t->invalidate();
}

void App::toast(const char* msg, uint16_t col, uint32_t ms) {
    toast_msg = msg;
    toast_color = col;
    toast_until = millis() + ms;
    if (Screen* t = router.top()) t->invalidate();
}

bool App::toast_active() const { return toast_until && millis() < toast_until; }

void App::draw_toast(Canvas& c) {
    if (!toast_active()) return;
    int w = int(toast_msg.size()) * 6 + 16;
    if (w > kW - 8) w = kW - 8;
    int h = 16, x = (kW - w) / 2, y = kH - kFooterH - h - 4;
    c.fillRoundRect(x, y, w, h, 4, toast_color);
    c.setFont(CV_FONT_SMALL);
    c.setTextColor(color::bg);
    c.setTextDatum(middle_center);
    c.drawString(toast_msg.c_str(), kW / 2, y + h / 2);
    c.setTextDatum(top_left);
}

void App::apply_brightness() { M5Cardputer.Display.setBrightness(uint8_t(settings.brightness)); }

}  // namespace ui
}  // namespace cv

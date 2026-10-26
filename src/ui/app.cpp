#include "ui/app.h"

#include "core/flows.h"
#include "core/messages.h"
#include "vault/serializer.h"

#include <M5Cardputer.h>

#ifndef CV_TEST_DELAY_DIV
#define CV_TEST_DELAY_DIV 1
#endif

namespace cv {
namespace ui {

App& app() {
    static App instance;
    return instance;
}

bool App::lock_now() {
    lock_pending = false;
    if (!persist_before_lock(session, store, flash_missing)) {
        toast("Not locked: vault is only in memory", color::bad, 3000);
        return false;
    }
    lock_session(router, session, make_unlock());
    return true;
}

bool App::save() {
    VaultError e = session.save(store);
    if (e != VaultError::Ok) {
        size_t size = serialized_size(session.data());
        if (e == VaultError::TooLarge && size > kMaxPayload) {
            // Say by how much, so the user knows what to shorten or remove.
            char msg[40];
            snprintf(msg, sizeof msg, "Vault full: %u bytes too much",
                     unsigned(size - kMaxPayload));
            toast(msg, color::bad, 3000);
        } else {
            toast(vault_error_str(e), color::bad, 3000);
        }
        return false;
    }
    flash_missing = false;
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
    WipeResult r = wipe_vault(store, guard);
    if (!r.erased || !r.reset) {
        // Stop instead of rebooting: a boot with an exhausted counter erases
        // again, so restarting here could loop and wear out the flash.
        const char* title = r.erased ? msg::kWipeErasedTitle : msg::kWipeFailedTitle;
        const char* body = r.erased  ? msg::kWipeCounterStuck
                           : r.armed ? msg::kWipeRetry
                                     : msg::kWipeNotArmed;
        if (canvas) {
            clear(*canvas);
            dialog(*canvas, title, body, color::bad);
            canvas->pushSprite(0, 0);
        }
        for (;;) {
            delay(1000);
        }
    }
    if (!store.mounted() && canvas) {
        // The data is gone; the next boot formats the blank partition.
        clear(*canvas);
        dialog(*canvas, msg::kWipeErasedTitle, msg::kWipeRemount, color::warn);
        canvas->pushSprite(0, 0);
    }
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

void App::arm_attempt_delay() {
    // Test builds shorten the real schedule (verified separately by host
    // tests) so end-to-end runs don't take a quarter of an hour.
    gate.arm(millis(), guard.required_delay_ms() / CV_TEST_DELAY_DIV);
}

uint32_t App::attempt_wait_ms() const { return gate.wait_ms(millis()); }

void App::apply_brightness() { M5Cardputer.Display.setBrightness(uint8_t(settings.brightness)); }

}  // namespace ui
}  // namespace cv

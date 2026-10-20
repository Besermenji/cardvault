// CardVault — offline encrypted secrets vault for the M5Stack Cardputer ADV.
#include <M5Cardputer.h>

#include "crypto/rng.h"
#include "platform/device.h"
#include "ui/app.h"

using namespace cv;
using namespace cv::ui;

namespace {

M5Canvas g_canvas(&M5Cardputer.Display);
constexpr uint32_t kDimBeforeLockMs = 10000;
bool g_toast_was_active = false;

#ifdef CV_SCREENSHOT
// Debug builds only: dump the canvas as RGB565 hex rows over USB serial.
// tools/screenshot.py turns the dump into a PNG.
void dump_screenshot() {
    Serial.printf("CVSHOT %d %d\n", kW, kH);
    char hex[kW * 4 + 1];
    for (int y = 0; y < kH; y++) {
        for (int x = 0; x < kW; x++) {
            snprintf(hex + x * 4, 5, "%04x", unsigned(g_canvas.readPixel(x, y)));
        }
        Serial.println(hex);
    }
    Serial.println("CVEND");
}
#endif

void handle_autolock(App& a, uint32_t now) {
    if (!a.session.is_open()) {
        if (a.dimmed) {
            a.dimmed = false;
            a.apply_brightness();
        }
        return;
    }
    uint32_t idle = now - a.last_input_ms;
    uint32_t limit = a.settings.autolock_s * 1000;
    if (idle >= limit) {
        a.dimmed = false;
        a.apply_brightness();
        a.lock_now();
        a.toast("Locked (idle)", color::accent);
    } else if (idle + kDimBeforeLockMs >= limit && !a.dimmed) {
        a.dimmed = true;
        M5Cardputer.Display.setBrightness(8);
    }
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
#ifdef CV_SCREENSHOT
    Serial.begin(115200);
#endif
    device::radios_off();

    App& a = app();
    a.settings = device::load_settings();
    M5Cardputer.Display.setRotation(1);
    a.apply_brightness();
    g_canvas.setColorDepth(16);
    g_canvas.createSprite(kW, kH);
    a.canvas = &g_canvas;

    // Seed the RNG pool with boot-time noise on top of the hardware RNG.
    uint32_t seed[4] = {uint32_t(micros()), esp_random(), uint32_t(ESP.getCycleCount()),
                        uint32_t(heap_caps_get_free_size(MALLOC_CAP_8BIT))};
    rng_stir(seed, sizeof seed);

    clear(g_canvas);
    g_canvas.pushSprite(0, 0);

    a.guard_kv.begin();
    if (!FlashStore::has_partition()) {
        a.router.reset(make_message(
            "No vault partition",
            "This install has no 'vault' partition. Flash the full CardVault image, or install "
            "the CardVault .bin with Launcher so its partition table is used.",
            color::bad));
        a.router.apply();
        return;
    }
    if (!a.store.begin()) {
        // Never format a non-blank partition silently: it may hold a damaged
        // vault (recoverable by experts) or another app's data under Launcher.
        a.router.reset(make_confirm(
            "Storage not readable",
            "The vault partition holds data CardVault cannot read. Format it? This destroys "
            "anything stored there. (N = leave it alone)",
            [] {
                app().busy("Formatting...");
                app().store.format();
                ESP.restart();
            },
            color::bad));
        a.router.apply();
        return;
    }
    if (a.guard.exhausted()) {
        // Power was cut between the last failed attempt and the wipe.
        a.wipe_and_reboot("Too many wrong passphrases.");
    }
    a.router.reset(a.store.exists() ? make_unlock() : make_setup());
    a.router.apply();
    a.last_input_ms = millis();
}

void loop() {
    M5Cardputer.update();
    App& a = app();
    uint32_t now = millis();

    // G0 side button: instant lock.
    if (M5Cardputer.BtnA.wasPressed() && a.session.is_open()) {
        a.lock_now();
        a.toast("Locked", color::accent);
    }

    Key k;
    while (poll_key(k)) {
        uint32_t noise[2] = {uint32_t(micros()), uint32_t(ESP.getCycleCount())};
        rng_stir(noise, sizeof noise);
        a.last_input_ms = now;
        if (a.dimmed) {
            a.dimmed = false;
            a.apply_brightness();
            continue;  // the waking key press is swallowed
        }
#ifdef CV_SCREENSHOT
        if (k.ctrl && (k.ch == 's' || k.ch == 'S')) {
            dump_screenshot();
            a.toast("Screenshot sent", color::info);
            continue;
        }
#endif
        if (Screen* s = a.router.top()) s->on_key(k);
        a.router.apply();
        if (a.lock_pending) a.lock_now();
        a.router.apply();
    }

    if (Screen* s = a.router.top()) s->tick(now);
    a.router.apply();
    if (a.lock_pending) a.lock_now();
    a.router.apply();
    handle_autolock(a, now);
    a.router.apply();

    bool toast_active = a.toast_active();
    Screen* s = a.router.top();
    if (s && (s->dirty || toast_active != g_toast_was_active)) {
        s->dirty = false;
        s->draw(g_canvas);
        a.draw_toast(g_canvas);
        g_canvas.pushSprite(0, 0);
    }
    g_toast_was_active = toast_active;
    delay(5);
}

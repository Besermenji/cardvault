// CardVault — offline encrypted secrets vault for the M5Stack Cardputer ADV.
#include <M5Cardputer.h>

#include "core/flows.h"
#include "core/messages.h"
#include "crypto/rng.h"
#include "platform/device.h"
#include "platform/nvs_kv.h"
#include "test_hooks.h"
#include "ui/app.h"
#include "ui/gfx.h"

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
        row_hex(g_canvas, y, hex);
        Serial.println(hex);
    }
    Serial.println("CVEND");
}
#endif

void handle_autolock(App& a, uint32_t now) {
    switch (autolock_step(a.session.is_open(), a.dimmed, now - a.last_input_ms,
                          a.settings.autolock_s * 1000, kDimBeforeLockMs)) {
        case AutolockAction::None: break;
        case AutolockAction::Wake:
            a.dimmed = false;
            a.apply_brightness();
            break;
        case AutolockAction::Dim:
            a.dimmed = true;
            M5Cardputer.Display.setBrightness(8);
            // Whatever was revealed (also on screens underneath) is hidden again.
            for (size_t i = 0; i < a.router.depth(); i++) a.router.at(i)->conceal();
            break;
        case AutolockAction::Clear:
            // Locked and idle: forget typed passphrases and open prompts.
            a.router.pop_to_root();
            if (Screen* t = a.router.top()) t->conceal();
            drop_pending_keys();
            a.dimmed = true;
            M5Cardputer.Display.setBrightness(8);
            break;
        case AutolockAction::Lock:
            a.dimmed = false;
            a.apply_brightness();
            // The lock screen starts a fresh idle period: it must not clear and
            // dim at once, and the toast stays readable.
            a.last_input_ms = now;
            if (a.lock_now()) a.toast("Locked (idle)", color::accent);
            break;
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

    bool guard_ok = a.guard_kv.begin();
    bool has_partition = FlashStore::has_partition();
    bool mounted = has_partition && a.store.begin();
#ifdef CV_NVS_GUARD_LEGACY
    // Only an upgrade from rc1 (a vault is already there) has a counter to carry over.
    if (guard_ok) {
        NvsKv legacy(CV_NVS_GUARD_LEGACY);
        if (legacy.begin(true)) a.guard.adopt(legacy, mounted && a.store.exists());
    }
#endif
    switch (boot_route(has_partition, guard_ok, mounted, guard_ok && a.guard.exhausted(),
                       mounted && a.store.exists())) {
        case BootRoute::NoPartition:
            a.router.reset(make_message(
                "No vault partition",
                "This install has no 'vault' partition. Flash the full CardVault image, or "
                "install the CardVault .bin with Launcher so its partition table is used.",
                color::bad));
            break;
        case BootRoute::GuardUnavailable:
            a.router.reset(make_message(msg::kGuardUnavailableTitle, msg::kGuardUnavailable,
                                        color::bad));
            break;
        case BootRoute::StorageUnreadable:
            // Never format a non-blank partition silently: it may hold a damaged
            // vault or another app's data.
            a.router.reset(make_confirm(msg::kStorageSetupTitle, msg::kStorageSetup,
                                        [] {
                                            app().busy("Setting up...");
                                            app().store.format();
                                            ESP.restart();
                                        },
                                        color::warn));
            break;
        case BootRoute::Wipe:
            // The counter reached the limit (possibly with power cut during the
            // last check, which counts as a failure) or a wipe was started but
            // did not finish. This runs even when the partition does not mount:
            // it is our own "vault" partition, and an exhausted counter means
            // its contents must go, so no confirmation is asked.
            a.wipe_and_reboot(msg::kWipeResume);
        case BootRoute::Unlock: a.router.reset(make_unlock()); break;
        case BootRoute::Setup: a.router.reset(make_setup()); break;
    }
    a.router.apply();
    a.last_input_ms = millis();
#ifdef CV_TEST_HOOKS
    test::begin();
#endif
}

void loop() {
    M5Cardputer.update();
    App& a = app();
    // Keys queued or held for the old screen must not reach the next one.
    auto navigate = [&a] {
        if (a.router.apply()) drop_pending_keys();
    };
    uint32_t now = millis();

#ifdef CV_TEST_HOOKS
    test::poll(a);
    bool g0 = M5Cardputer.BtnA.wasPressed() || test::take_g0();
#else
    bool g0 = M5Cardputer.BtnA.wasPressed();
#endif
    // G0 side button: instant lock.
    if (g0 && a.session.is_open() && a.lock_now()) {
        a.last_input_ms = now;
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
        navigate();
        if (a.lock_pending) a.lock_now();
        navigate();
    }

    if (Screen* s = a.router.top()) s->tick(now);
    navigate();
    if (a.lock_pending) a.lock_now();
    navigate();
    handle_autolock(a, now);
    navigate();

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

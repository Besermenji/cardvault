#include "platform/device.h"

#include <M5Cardputer.h>
#include <algorithm>
#include <Preferences.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <sys/time.h>

#include "features/timeutil.h"

namespace cv {
namespace device {

void radios_off() {
    // Wi-Fi and Bluetooth are never initialised by this firmware. Releasing
    // the BT controller's reserved RAM also gives Argon2 more heap.
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
}

Settings load_settings() {
    Settings s;
    Preferences p;
    if (p.begin(CV_NVS_CFG, true)) {
        s.autolock_s = p.getUInt("autolock", s.autolock_s);
        s.brightness = p.getUInt("bright", s.brightness);
        p.end();
    }
    return s;
}

void save_settings(const Settings& s) {
    Preferences p;
    if (!p.begin(CV_NVS_CFG, false)) return;
    p.putUInt("autolock", s.autolock_s);
    p.putUInt("bright", s.brightness);
    p.end();
}

uint32_t time_kdf(const KdfParams& p) {
    static const uint8_t kPass[] = {'c', 'a', 'l', 'i', 'b', 'r', 'a', 't', 'e'};
    static const uint8_t kSalt[kSaltSize] = {0};
    uint8_t out[32];
    int64_t t0 = esp_timer_get_time();
    if (!kdf_derive(out, kPass, sizeof kPass, kSalt, p)) return 0;
    int64_t us = esp_timer_get_time() - t0;
    return us < 1 ? 1 : uint32_t(us);
}

KdfTuning calibrate_kdf(uint32_t target_ms, uint32_t min_mem_kib) {
    uint32_t floor = std::max(kMinKdfMemKib, std::min(min_mem_kib, kMaxKdfMemKib));
    uint32_t mem = std::max(kdf_pick_mem(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)), floor);
    for (;;) {
        KdfTuning t = kdf_tune(mem, target_ms, time_kdf);
        if (t.status != KdfTuneStatus::Failed || mem <= floor) return t;
        // The work area did not fit after all: try a smaller one.
        mem = mem > floor + 32 ? mem - 32 : floor;
    }
}

bool time_valid() { return time(nullptr) >= time_t(kClockFloor); }

uint32_t unix_time() { return time_valid() ? uint32_t(time(nullptr)) : 0; }

void set_unix_time(uint32_t t) {
    timeval tv{time_t(t), 0};
    settimeofday(&tv, nullptr);
}

int battery_percent() { return M5Cardputer.Power.getBatteryLevel(); }

}  // namespace device
}  // namespace cv

#include "platform/device.h"

#include <M5Cardputer.h>
#include <Preferences.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <sys/time.h>

namespace cv {
namespace device {

namespace {
constexpr uint32_t kTimeFloor = 1735689600;  // 2025-01-01: anything earlier means "not set"
}

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

KdfParams calibrate_kdf(uint32_t target_ms) {
    // Leave headroom for the UI and serialization buffers during unlock.
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t mem = 64;
    for (uint32_t candidate : {256u, 192u, 128u}) {
        if (largest >= candidate * 1024 + 64 * 1024) {
            mem = candidate;
            break;
        }
    }
    KdfParams probe{mem, kMinKdfPasses};
    uint8_t out[32], salt[kSaltSize] = {0};
    uint32_t t0 = millis();
    kdf_derive(out, reinterpret_cast<const uint8_t*>("calibrate"), 9, salt, probe);
    uint32_t elapsed = millis() - t0;
    if (elapsed == 0) elapsed = 1;
    uint32_t passes = uint32_t(uint64_t(target_ms) * kMinKdfPasses / elapsed);
    if (passes < kMinKdfPasses) passes = kMinKdfPasses;
    if (passes > kdf_max_passes(mem)) passes = kdf_max_passes(mem);
    return KdfParams{mem, passes};
}

bool time_valid() { return time(nullptr) >= time_t(kTimeFloor); }

uint32_t unix_time() { return time_valid() ? uint32_t(time(nullptr)) : 0; }

void set_unix_time(uint32_t t) {
    timeval tv{time_t(t), 0};
    settimeofday(&tv, nullptr);
}

int battery_percent() { return M5Cardputer.Power.getBatteryLevel(); }

}  // namespace device
}  // namespace cv

// Device-level helpers: settings, clock, KDF calibration, radio shutdown.
#pragma once

#include <cstdint>

#include "crypto/kdf.h"

namespace cv {

struct Settings {
    uint32_t autolock_s = 120;
    uint32_t brightness = 120;
};

namespace device {

void radios_off();
Settings load_settings();
void save_settings(const Settings& s);

// Picks Argon2 memory from available heap and passes for ~target_ms unlock time.
KdfParams calibrate_kdf(uint32_t target_ms);

bool time_valid();
uint32_t unix_time();  // 0 if the clock was never set
void set_unix_time(uint32_t t);

int battery_percent();

}  // namespace device
}  // namespace cv

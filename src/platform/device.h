// Device-level helpers: settings, clock, KDF calibration, radio shutdown.
#pragma once

#include <cstdint>

#include "crypto/kdf.h"
#include "crypto/kdf_calibrate.h"

// NVS namespaces. Test builds override them so they never touch real state.
#ifndef CV_NVS_GUARD
#define CV_NVS_GUARD "cv_guard"
// Where v0.1.0-rc1 kept the counter; adopted once on upgrade. Not defined
// for test builds, which must not read real state.
#define CV_NVS_GUARD_LEGACY "guard"
#endif
#ifndef CV_NVS_CFG
#define CV_NVS_CFG "cv_cfg"
#endif

namespace cv {

// Limits that keep the device usable: a 0 s auto-lock would lock at once, a
// dark display cannot be read.
constexpr uint32_t kMinAutolockS = 30;
constexpr uint32_t kMaxAutolockS = 1800;
constexpr uint32_t kMinBrightness = 20;
constexpr uint32_t kMaxBrightness = 255;

struct Settings {
    uint32_t autolock_s = 120;
    uint32_t brightness = 120;
};

inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// Pulls stored values back into the allowed range.
inline Settings clamp_settings(Settings s) {
    s.autolock_s = clamp_u32(s.autolock_s, kMinAutolockS, kMaxAutolockS);
    s.brightness = clamp_u32(s.brightness, kMinBrightness, kMaxBrightness);
    return s;
}

namespace device {

void radios_off();
Settings load_settings();
void save_settings(const Settings& s);

// Times one kdf_derive with these parameters (µs, 0 if it could not run).
uint32_t time_kdf(const KdfParams& p);
// Picks Argon2 memory from the free heap (at least min_mem_kib) and times the
// device to choose passes for ~target_ms unlock time. Steps the memory down,
// not below min_mem_kib, if the work area cannot be allocated.
// status == Failed means no usable parameters.
KdfTuning calibrate_kdf(uint32_t target_ms, uint32_t min_mem_kib = 0);

bool time_valid();
uint32_t unix_time();  // 0 if the clock was never set
void set_unix_time(uint32_t t);

int battery_percent();

}  // namespace device
}  // namespace cv

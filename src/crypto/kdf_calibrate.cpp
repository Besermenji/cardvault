#include "crypto/kdf_calibrate.h"

#include <algorithm>

namespace cv {

namespace {

constexpr uint32_t kMemStepKib = 32;
constexpr uint32_t kProbeMinExtraPasses = 8;

// Passes for the target given a linear time model, clamped to what is allowed.
uint32_t passes_for(uint64_t target_us, uint32_t overhead_us, uint32_t per_pass_us,
                    uint32_t mem_kib, KdfTuneStatus& status) {
    uint64_t want = target_us > overhead_us ? (target_us - overhead_us + per_pass_us / 2) / per_pass_us
                                            : 0;
    uint32_t max = kdf_max_passes(mem_kib);
    status = KdfTuneStatus::Ok;
    if (want < kMinKdfPasses) {
        status = KdfTuneStatus::RaisedToMin;
        return kMinKdfPasses;
    }
    if (want > max) {
        status = KdfTuneStatus::CappedAtMax;
        return max;
    }
    return uint32_t(want);
}

bool in_band(uint64_t us, uint64_t target_us) {
    return us >= target_us * 3 / 4 && us <= target_us * 27 / 20;
}

}  // namespace

uint32_t kdf_pick_mem(size_t largest_free_bytes) {
    uint64_t kib = largest_free_bytes / 1024;
    uint64_t reserve = kKdfHeapReserve / 1024;
    uint64_t mem = kib > reserve ? (kib - reserve) / kMemStepKib * kMemStepKib : 0;
    if (mem > kMaxKdfMemKib) mem = kMaxKdfMemKib;
    if (mem < kMinKdfMemKib) mem = kMinKdfMemKib;
    return uint32_t(mem);
}

KdfTuning kdf_tune(uint32_t mem_kib, uint32_t target_ms, const KdfTimer& time) {
    KdfTuning t;
    t.params = KdfParams{mem_kib, kMinKdfPasses};
    if (!kdf_params_valid(t.params) || target_ms == 0) return t;
    const uint64_t target_us = uint64_t(target_ms) * 1000;

    // Warm-up: the first derive pays for cold caches and a cold heap.
    if (time(KdfParams{mem_kib, kMinKdfPasses}) == 0) return t;

    t.probe_a_us = time(KdfParams{mem_kib, kMinKdfPasses});
    if (t.probe_a_us == 0) return t;
    if (t.probe_a_us >= target_us) {
        // Even the minimum is too slow: no point timing more passes.
        t.per_pass_us = std::max<uint32_t>(1, t.probe_a_us / kMinKdfPasses);
        t.status = KdfTuneStatus::RaisedToMin;
        return t;
    }

    // Second probe sized to take about a quarter of the target. At least 8
    // extra passes for precision, unless that alone would pass half the
    // target. Valid memory (<= 4096 KiB) always leaves room for 21 extra.
    uint32_t est = std::max<uint32_t>(1, t.probe_a_us / kMinKdfPasses);
    uint64_t k = target_us / 4 / est;
    if (k < kProbeMinExtraPasses && uint64_t(kProbeMinExtraPasses) * est <= target_us / 2)
        k = kProbeMinExtraPasses;
    k = std::min<uint64_t>(std::max<uint64_t>(k, 1), kdf_max_passes(mem_kib) - kMinKdfPasses);
    t.probe_b_passes = kMinKdfPasses + uint32_t(k);
    t.probe_b_us = time(KdfParams{mem_kib, t.probe_b_passes});
    if (t.probe_b_us == 0) return t;

    if (t.probe_b_us > t.probe_a_us) {
        t.per_pass_us = uint32_t((uint64_t(t.probe_b_us - t.probe_a_us) + k / 2) / k);
        uint64_t base = uint64_t(kMinKdfPasses) * t.per_pass_us;
        t.overhead_us = t.probe_a_us > base ? uint32_t(t.probe_a_us - base) : 0;
    } else {
        // Noise: no usable slope, so assume all of the time is per-pass.
        t.per_pass_us = t.probe_b_us / t.probe_b_passes;
        t.overhead_us = 0;
    }
    if (t.per_pass_us == 0) t.per_pass_us = 1;

    t.params.passes = passes_for(target_us, t.overhead_us, t.per_pass_us, mem_kib, t.status);
    return t;
}

bool kdf_check(KdfTuning& t, uint32_t measured_us, uint32_t target_ms, const KdfTimer& time) {
    t.check_us = measured_us;
    if (t.status != KdfTuneStatus::Ok || measured_us == 0 || t.retuned) return false;
    const uint64_t target_us = uint64_t(target_ms) * 1000;
    if (in_band(measured_us, target_us)) return false;
    if (measured_us > target_us) {
        uint32_t again = time(t.params);
        if (again != 0 && again < measured_us) t.check_us = again;
        if (in_band(t.check_us, target_us)) return false;
    }
    uint64_t net = t.check_us > t.overhead_us ? t.check_us - t.overhead_us : t.check_us;
    uint32_t old = t.params.passes;
    t.per_pass_us = uint32_t(std::max<uint64_t>(1, net / old));
    t.params.passes = passes_for(target_us, t.overhead_us, t.per_pass_us, t.params.mem_kib, t.status);
    t.retuned = true;
    return t.params.passes != old;
}

uint32_t kdf_estimate_ms(const KdfTuning& t) {
    return uint32_t((uint64_t(t.overhead_us) + uint64_t(t.params.passes) * t.per_pass_us) / 1000);
}

}  // namespace cv

#include "crypto/kdf_calibrate.h"

#include <algorithm>

namespace cv {

namespace {

constexpr uint32_t kMemStepKib = 32;
constexpr uint32_t kPreferredMinMemKib = 128;
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

}  // namespace

uint32_t kdf_pick_mem(size_t largest_free_bytes) {
    uint64_t kib = largest_free_bytes / 1024;
    uint64_t reserve = kKdfHeapReserve / 1024;
    uint64_t mem = kib > reserve ? (kib - reserve) / kMemStepKib * kMemStepKib : 0;
    if (mem > kMaxKdfMemKib) mem = kMaxKdfMemKib;
    if (mem < kPreferredMinMemKib) {
        // Short on heap: take the preferred floor only if it still fits with a
        // smaller reserve, otherwise fall back to the format minimum.
        return kib >= kPreferredMinMemKib + 32 ? kPreferredMinMemKib : kMinKdfMemKib;
    }
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

    // Second probe sized to take about a quarter of the target; bounded by the
    // work cap so the probe itself stays valid.
    uint32_t est = std::max<uint32_t>(1, t.probe_a_us / kMinKdfPasses);
    uint64_t extra = target_us / 4 / est;
    uint32_t room = kdf_max_passes(mem_kib) - kMinKdfPasses;
    uint32_t k = uint32_t(std::min<uint64_t>(std::max<uint64_t>(extra, kProbeMinExtraPasses), room));
    t.probe_b_passes = kMinKdfPasses + k;
    if (k > 0) {
        t.probe_b_us = time(KdfParams{mem_kib, t.probe_b_passes});
        if (t.probe_b_us == 0) return t;
    }

    if (k > 0 && t.probe_b_us > t.probe_a_us) {
        t.per_pass_us = uint32_t((uint64_t(t.probe_b_us - t.probe_a_us) + k / 2) / k);
        uint64_t base = uint64_t(kMinKdfPasses) * t.per_pass_us;
        t.overhead_us = t.probe_a_us > base ? uint32_t(t.probe_a_us - base) : 0;
    } else {
        // Noise (or no room for a second probe): no usable slope, so assume
        // all of the time is per-pass.
        uint32_t b = k > 0 ? t.probe_b_us : t.probe_a_us;
        uint32_t n = k > 0 ? t.probe_b_passes : kMinKdfPasses;
        t.per_pass_us = std::max<uint32_t>(1, b / n);
        t.overhead_us = 0;
    }
    if (t.per_pass_us == 0) t.per_pass_us = 1;

    t.params.passes = passes_for(target_us, t.overhead_us, t.per_pass_us, mem_kib, t.status);

    // Closed loop: the model is a fit, the check is the real thing.
    t.check_us = time(t.params);
    if (t.check_us == 0) {
        t.status = KdfTuneStatus::Failed;
        return t;
    }
    if (t.check_us < target_us * 3 / 4 || t.check_us > target_us * 27 / 20) {
        uint64_t net = t.check_us > t.overhead_us ? t.check_us - t.overhead_us : t.check_us;
        uint32_t per_pass = uint32_t(std::max<uint64_t>(1, net / t.params.passes));
        t.params.passes = passes_for(target_us, t.overhead_us, per_pass, mem_kib, t.status);
        t.retuned = true;
    }
    return t;
}

}  // namespace cv

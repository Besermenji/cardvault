// Picks Argon2 parameters so an unlock takes about the target time on this
// device. Pure logic: the caller supplies the timing, so it runs in host tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "crypto/kdf.h"

namespace cv {

// Microseconds one derive with these parameters takes; 0 if it could not run
// (for example the work area could not be allocated).
using KdfTimer = std::function<uint32_t(const KdfParams&)>;

enum class KdfTuneStatus {
    Ok,           // passes landed inside the allowed range
    RaisedToMin,  // device too slow for the target: unlock takes longer than asked
    CappedAtMax,  // device too fast for the target: unlock takes less than asked
    Failed,       // a probe could not run; params are not usable
};

struct KdfTuning {
    KdfParams params{0, 0};
    KdfTuneStatus status = KdfTuneStatus::Failed;
    uint32_t probe_a_us = 0;       // kMinKdfPasses passes
    uint32_t probe_b_us = 0;       // probe_b_passes passes
    uint32_t probe_b_passes = 0;
    uint32_t per_pass_us = 0;      // fitted cost of one pass
    uint32_t overhead_us = 0;      // fitted fixed cost (allocation, wipe)
    uint32_t check_us = 0;         // closed-loop check of the chosen params
    bool retuned = false;          // the check was outside the band, passes were rescaled
};

// Heap kept free for the UI, the vault image and the SD driver while Argon2
// holds its work area. A vault records its memory size, so every later unlock
// needs the same amount: keep this generous.
constexpr size_t kKdfHeapReserve = 96 * 1024;

// Argon2 memory (KiB) for a heap whose largest free block is this many bytes.
// Always valid for kdf_params_valid() memory rules.
uint32_t kdf_pick_mem(size_t largest_free_bytes);

// Warm-up, then two timed probes (3 and 3+K passes) to separate the fixed
// overhead from the per-pass cost, then one timed check of the result; if the
// check is outside 0.75-1.35x the target, the passes are rescaled once.
KdfTuning kdf_tune(uint32_t mem_kib, uint32_t target_ms, const KdfTimer& time);

}  // namespace cv

// Brute-force protection for the unlock screen.
//
// The failure counter is incremented and persisted *before* each passphrase
// check and reset only after a successful unlock, so cutting power mid-check
// still counts as a failure. After kMaxAttempts consecutive failures the
// caller must wipe the vault.
#pragma once

#include <cstdint>

#include "security/kv.h"

namespace cv {

constexpr uint32_t kMaxAttempts = 10;
// Failures assumed when no counter is stored (NVS erased or damaged while the
// vault stayed): one attempt left, behind the longest delay. Erasing NVS can
// therefore buy at most one guess at a time. (Someone able to write flash can
// also roll back the vault itself; that is out of scope without secure boot.)
// Creating a vault and a successful unlock write a real value.
constexpr uint32_t kFailuresIfMissing = kMaxAttempts - 1;

// Delay required before the next attempt, given the failures so far.
uint32_t attempt_delay_ms(uint32_t failures);

class AttemptGuard {
public:
    explicit AttemptGuard(KvStore& kv) : kv_(kv) {}

    // Failures so far. A missing counter reads as kFailuresIfMissing; so does
    // one that cannot be read, but then begin_attempt() refuses, so the value
    // is only shown, never counted from.
    uint32_t failures();
    uint32_t remaining() { return exhausted() ? 0 : kMaxAttempts - failures(); }
    bool exhausted() { return failures() >= kMaxAttempts; }
    uint32_t required_delay_ms() { return attempt_delay_ms(failures()); }

    // Call right before checking a passphrase. Returns false if the counter
    // could not be read or persisted; the caller must then refuse the attempt.
    bool begin_attempt();
    // Call after the passphrase was accepted.
    void record_success();
    // Undo begin_attempt() when the check could not run for reasons unrelated
    // to the passphrase (out of memory, storage error).
    void refund();
    // Persist the exhausted state (used to record the intent to wipe before
    // anything is destroyed).
    bool mark_exhausted();
    bool reset();
    // Copies the counter from `legacy` (a store used by an older release)
    // when this one has none, so upgrading does not hit the missing-counter
    // penalty. Returns true if a value was adopted.
    bool adopt(KvStore& legacy);

private:
    bool read(uint32_t& out);

    KvStore& kv_;
};

// In-memory clock for attempt_delay_ms(), shared by every screen that checks
// a guarded passphrase so none of them is a faster guessing channel.
struct AttemptGate {
    uint32_t ready_at = 0;
    void arm(uint32_t now, uint32_t delay_ms) { ready_at = now + delay_ms; }
    uint32_t wait_ms(uint32_t now) const {
        int32_t d = int32_t(ready_at - now);
        return d > 0 ? uint32_t(d) : 0;
    }
};

}  // namespace cv

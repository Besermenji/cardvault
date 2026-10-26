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

// Delay required before the next attempt, given the failures so far.
uint32_t attempt_delay_ms(uint32_t failures);

class AttemptGuard {
public:
    explicit AttemptGuard(KvStore& kv) : kv_(kv) {}

    uint32_t failures();
    uint32_t remaining() { return exhausted() ? 0 : kMaxAttempts - failures(); }
    bool exhausted() { return failures() >= kMaxAttempts; }
    uint32_t required_delay_ms() { return attempt_delay_ms(failures()); }

    // Call right before checking a passphrase. Returns false if the counter
    // could not be persisted; the caller must then refuse the attempt.
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

private:
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

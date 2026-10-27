#include "security/attempt_guard.h"

namespace cv {

namespace {
const char* kKey = "fails";
}

uint32_t attempt_delay_ms(uint32_t failures) {
    static const uint32_t kSchedule[] = {0,      0,      0,       5000,   15000,
                                         30000,  60000,  120000,  300000};
    const uint32_t n = sizeof kSchedule / sizeof kSchedule[0];
    return kSchedule[failures < n ? failures : n - 1];
}

bool AttemptGuard::read(uint32_t& out) {
    out = 0;
    switch (kv_.get_u32(kKey, out)) {
        case KvRead::Ok: return true;
        case KvRead::Missing: out = kFailuresIfMissing; return true;
        case KvRead::Error: break;
    }
    out = kFailuresIfMissing;
    return false;
}

uint32_t AttemptGuard::failures() {
    uint32_t f;
    read(f);
    return f;
}

bool AttemptGuard::begin_attempt() {
    uint32_t f;
    if (!read(f) || f >= kMaxAttempts) return false;
    return kv_.set_u32(kKey, f + 1);
}

void AttemptGuard::record_success() { reset(); }

void AttemptGuard::refund() {
    // Only undo a value we can actually read; never lower a guess.
    uint32_t f = 0;
    if (kv_.get_u32(kKey, f) == KvRead::Ok && f > 0) kv_.set_u32(kKey, f - 1);
}

bool AttemptGuard::mark_exhausted() { return kv_.set_u32(kKey, kMaxAttempts); }

bool AttemptGuard::reset() { return kv_.set_u32(kKey, 0); }

bool AttemptGuard::adopt(KvStore& legacy, bool vault_exists) {
    if (!vault_exists) return false;
    uint32_t mine, old;
    if (kv_.get_u32(kKey, mine) != KvRead::Missing) return false;
    if (legacy.get_u32(kKey, old) != KvRead::Ok) return false;
    return kv_.set_u32(kKey, old);
}

}  // namespace cv

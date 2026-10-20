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

uint32_t AttemptGuard::failures() {
    uint32_t v = 0;
    kv_.get_u32(kKey, v);
    return v;
}

bool AttemptGuard::begin_attempt() {
    uint32_t f = failures();
    if (f >= kMaxAttempts) return false;
    return kv_.set_u32(kKey, f + 1);
}

void AttemptGuard::record_success() { reset(); }

void AttemptGuard::reset() { kv_.set_u32(kKey, 0); }

}  // namespace cv

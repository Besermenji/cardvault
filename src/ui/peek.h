// Reveal timing shared by the secret viewers (hardware-independent).
#pragma once

#include <cstdint>

namespace cv {
namespace ui {

// `now` may predate the moment a reveal started (the main loop reads the clock
// before it handles keys), so compare as a signed difference.
inline bool reveal_expired(uint32_t now, uint32_t since, uint32_t limit_ms) {
    return int32_t(now - since) >= int32_t(limit_ms);
}

// Hold-to-peek: the secret shows while the key is held, for at most
// `limit_ms` per press. conceal() (screen dims) hides it until the key is
// released, so a key stuck down in a pocket does not keep it on screen.
class HoldPeek {
public:
    // Feeds the current key state; returns true when visibility changed.
    bool update(bool held, uint32_t now, uint32_t limit_ms) {
        bool was = shown();
        if (!held) {
            held_ = blocked_ = false;
        } else if (!held_) {
            held_ = true;
            since_ = now;
        }
        if (held_ && reveal_expired(now, since_, limit_ms)) blocked_ = true;
        return was != shown();
    }
    void conceal() { blocked_ = held_; }
    bool shown() const { return held_ && !blocked_; }

private:
    bool held_ = false;
    bool blocked_ = false;
    uint32_t since_ = 0;
};

}  // namespace ui
}  // namespace cv

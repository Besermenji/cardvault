// Turns keyboard scans into key events: newly pressed keys, auto-repeat of the
// last one while it stays held, and a queue the UI drains.
// Hardware-independent: unit tested on the host.
#pragma once

#include <cstdint>
#include <vector>

#include "ui/input.h"

namespace cv {
namespace ui {

struct KeyPos {
    int x = 0;
    int y = 0;
    bool operator==(const KeyPos& o) const { return x == o.x && y == o.y; }
};

class KeyStream {
public:
    static constexpr uint32_t kRepeatDelay = 450;
    static constexpr uint32_t kRepeatRate = 70;

    // One keyboard scan: `down` lists the keys pressed now. `make(pos, key)`
    // translates a newly pressed key and returns false for modifiers.
    template <class Make>
    void scan(const std::vector<KeyPos>& down, uint32_t now, Make make) {
        // Diff against the previous scan ourselves: counting pressed keys
        // misses fast key rolls.
        bool changed = down.size() != prev_.size();
        for (auto& pos : down) {
            if (contains(prev_, pos)) continue;  // only newly pressed keys
            changed = true;
            Key k;
            if (make(pos, k)) {
                queue_.push_back(k);
                last_ = k;
                last_pos_ = pos;
                has_last_ = repeats(k);
                repeat_at_ = now + kRepeatDelay;
            }
        }
        if (changed) {
            prev_ = down;
        } else if (has_last_ && contains(down, last_pos_) && now >= repeat_at_) {
            queue_.push_back(last_);
            repeat_at_ = now + kRepeatRate;
        }
        if (!contains(down, last_pos_)) has_last_ = false;
    }

    void inject(const Key& k) { queue_.push_back(k); }

    bool next(Key& out) {
        if (queue_.empty()) return false;
        out = queue_.front();
        queue_.erase(queue_.begin());
        return true;
    }

    // Forgets queued events and stops the held key from repeating; it repeats
    // again only after being released and pressed anew.
    void drop_pending() {
        queue_.clear();
        has_last_ = false;
    }

    size_t pending() const { return queue_.size(); }

private:
    static bool repeats(const Key& k) {
        return k.code != KeyCode::Opt && k.code != KeyCode::Tab && k.code != KeyCode::Enter &&
               k.code != KeyCode::Esc;
    }
    static bool contains(const std::vector<KeyPos>& v, const KeyPos& p) {
        for (auto& q : v)
            if (q == p) return true;
        return false;
    }

    std::vector<KeyPos> prev_;
    std::vector<Key> queue_;
    Key last_;
    KeyPos last_pos_;
    bool has_last_ = false;
    uint32_t repeat_at_ = 0;
};

}  // namespace ui
}  // namespace cv

// Keyboard events with fn-layer navigation and auto-repeat.
//
//   fn + ;  .  ,  /   -> Up Down Left Right
//   fn + `            -> Esc
//   opt (alone)       -> Opt (toggle reveal in password fields)
#pragma once

#include <cstdint>

namespace cv {
namespace ui {

enum class KeyCode : uint8_t { Char, Enter, Backspace, Esc, Up, Down, Left, Right, Tab, Opt };

struct Key {
    KeyCode code = KeyCode::Char;
    char ch = 0;
    bool fn = false;
    bool ctrl = false;
    bool alt = false;
    bool is(char c) const { return code == KeyCode::Char && ch == c; }
    bool is(KeyCode k) const { return code == k; }
    // Navigation that also accepts the unshifted arrow glyphs (for screens
    // without free text entry).
    bool up() const { return code == KeyCode::Up || is(';'); }
    bool down() const { return code == KeyCode::Down || is('.'); }
    bool left() const { return code == KeyCode::Left || is(','); }
    bool right() const { return code == KeyCode::Right || is('/'); }
    bool back() const { return code == KeyCode::Esc || is('`'); }
};

// Returns true and fills `out` when a key event is available.
bool poll_key(Key& out);
// Drops key events not yet delivered and stops the held key from
// auto-repeating. Called when the screen changes, so keys meant for the old
// screen (a held or rolled Y on a confirm) never reach the next one.
void drop_pending_keys();
// True while the physical key that produces `c` is held down.
bool key_held(char c);

#ifdef CV_TEST_HOOKS
// Test builds only: queue a synthetic key event / simulate a held key.
void inject_key(const Key& k);
void set_held_override(char c, bool held);
#endif

}  // namespace ui
}  // namespace cv

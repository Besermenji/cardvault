#include "ui/input.h"

#include <M5Cardputer.h>

#include <vector>

#include "ui/key_stream.h"

namespace cv {
namespace ui {

namespace {
KeyStream g_stream;

#ifdef CV_TEST_HOOKS
// Simulated physical key (test builds): scanned like a real one, so it takes
// the same press/repeat path. Marked with x = -1, y = the character.
char g_sim_down = 0;
#endif

bool make_key(const Point2D_t& pos, const Keyboard_Class::KeysState& st, Key& k) {
    auto& kb = M5Cardputer.Keyboard;
    KeyValue_t v = kb.getKeyValue(pos);
    uint8_t code = uint8_t(v.value_first);
    k = Key();
    k.fn = st.fn;
    k.ctrl = st.ctrl;
    k.alt = st.alt;
    switch (code) {
        case KEY_FN:
        case KEY_LEFT_CTRL:
        case KEY_LEFT_SHIFT:
        case KEY_LEFT_ALT:
            return false;
        case KEY_OPT:
            k.code = KeyCode::Opt;
            return true;
        case KEY_TAB:
            k.code = KeyCode::Tab;
            return true;
        case KEY_BACKSPACE:
            k.code = KeyCode::Backspace;
            return true;
        case KEY_ENTER:
            k.code = KeyCode::Enter;
            return true;
    }
    char c = (st.shift || kb.capslocked()) ? v.value_second : v.value_first;
    if (st.fn) {
        switch (v.value_first) {
            case ';': k.code = KeyCode::Up; return true;
            case '.': k.code = KeyCode::Down; return true;
            case ',': k.code = KeyCode::Left; return true;
            case '/': k.code = KeyCode::Right; return true;
            case '`': k.code = KeyCode::Esc; return true;
        }
    }
    k.code = KeyCode::Char;
    k.ch = c;
    return true;
}
}  // namespace

bool poll_key(Key& out) {
    auto& kb = M5Cardputer.Keyboard;
    std::vector<KeyPos> down;
    for (auto& p : kb.keyList()) down.push_back({p.x, p.y});
#ifdef CV_TEST_HOOKS
    if (g_sim_down) down.push_back({-1, g_sim_down});
#endif
    g_stream.scan(down, millis(), [&kb](const KeyPos& pos, Key& k) {
#ifdef CV_TEST_HOOKS
        if (pos.x == -1) {
            k = Key();
            k.ch = char(pos.y);
            return true;
        }
#endif
        Point2D_t p;
        p.x = pos.x;
        p.y = pos.y;
        return make_key(p, kb.keysState(), k);
    });
    return g_stream.next(out);
}

void drop_pending_keys() { g_stream.drop_pending(); }

#ifdef CV_TEST_HOOKS
namespace {
char g_held_override = 0;
}
void inject_key(const Key& k) { g_stream.inject(k); }
void set_sim_pressed(char c) { g_sim_down = c; }
void set_held_override(char c, bool held) { g_held_override = held ? c : 0; }
bool key_held(char c) {
    return (g_held_override && g_held_override == c) || M5Cardputer.Keyboard.isKeyPressed(c);
}
#else
bool key_held(char c) { return M5Cardputer.Keyboard.isKeyPressed(c); }
#endif

}  // namespace ui
}  // namespace cv

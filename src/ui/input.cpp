#include "ui/input.h"

#include <M5Cardputer.h>

#include <vector>

namespace cv {
namespace ui {

namespace {
constexpr uint32_t kRepeatDelay = 450;
constexpr uint32_t kRepeatRate = 70;

std::vector<Point2D_t> g_prev;
std::vector<Key> g_queue;
Key g_last;
bool g_has_last = false;
Point2D_t g_last_pos;
uint32_t g_repeat_at = 0;

bool contains(const std::vector<Point2D_t>& v, const Point2D_t& p) {
    for (auto& q : v)
        if (q.x == p.x && q.y == p.y) return true;
    return false;
}

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
    const auto& now_keys = kb.keyList();
    uint32_t now = millis();

    // Diff against the previous poll ourselves: the library's isChange() only
    // compares the number of pressed keys and misses fast key rolls.
    bool changed = now_keys.size() != g_prev.size();
    for (auto& pos : now_keys) {
        if (contains(g_prev, pos)) continue;  // only newly pressed keys
        changed = true;
        Key k;
        if (make_key(pos, kb.keysState(), k)) {
            g_queue.push_back(k);
            g_last = k;
            g_last_pos = pos;
            g_has_last = k.code != KeyCode::Opt && k.code != KeyCode::Tab &&
                         k.code != KeyCode::Enter && k.code != KeyCode::Esc;
            g_repeat_at = now + kRepeatDelay;
        }
    }
    if (changed) {
        g_prev.assign(now_keys.begin(), now_keys.end());
    } else if (g_has_last && contains(now_keys, g_last_pos) && now >= g_repeat_at) {
        g_queue.push_back(g_last);
        g_repeat_at = now + kRepeatRate;
    }
    if (!contains(now_keys, g_last_pos)) g_has_last = false;

    if (g_queue.empty()) return false;
    out = g_queue.front();
    g_queue.erase(g_queue.begin());
    return true;
}

#ifdef CV_TEST_HOOKS
namespace {
char g_held_override = 0;
}
void inject_key(const Key& k) { g_queue.push_back(k); }
void set_held_override(char c, bool held) { g_held_override = held ? c : 0; }
bool key_held(char c) {
    return (g_held_override && g_held_override == c) || M5Cardputer.Keyboard.isKeyPressed(c);
}
#else
bool key_held(char c) { return M5Cardputer.Keyboard.isKeyPressed(c); }
#endif

}  // namespace ui
}  // namespace cv

#ifdef CV_TEST_HOOKS

#include "ui/sim_keyboard.h"

#include <algorithm>

namespace cv {
namespace ui {

namespace {
std::vector<Point2D_t> g_sim;    // keys the host holds down
std::vector<Point2D_t> g_added;  // sim keys currently in the library's key list
}  // namespace

void set_sim_keys(const std::vector<Point2D_t>& keys) { g_sim = keys; }

void apply_sim_keys() {
    auto& kb = M5Cardputer.Keyboard;
    if (g_sim.empty() && g_added.empty()) return;
    // The library hands out its key list read-only; this test-only overlay edits it.
    auto& list = const_cast<std::vector<Point2D_t>&>(kb.keyList());
    for (const auto& p : g_added) {
        auto it = std::find(list.begin(), list.end(), p);
        if (it != list.end()) list.erase(it);
    }
    g_added.clear();
    for (const auto& p : g_sim) {
        if (std::find(list.begin(), list.end(), p) == list.end()) {
            list.push_back(p);
            g_added.push_back(p);
        }
    }
    kb.updateKeysState();
}

}  // namespace ui
}  // namespace cv

#endif

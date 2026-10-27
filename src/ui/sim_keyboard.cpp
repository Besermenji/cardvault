#ifdef CV_TEST_HOOKS

#include "ui/sim_keyboard.h"

#include <algorithm>
#include <memory>

namespace cv {
namespace ui {

namespace {
SimKeyboardReader* g_reader = nullptr;
}

void SimKeyboardReader::update() {
    // Take the previous simulated keys out so a real key that is released
    // meanwhile is handled by the base class on its own.
    for (const auto& p : added_) {
        auto it = std::find(_key_list.begin(), _key_list.end(), p);
        if (it != _key_list.end()) _key_list.erase(it);
    }
    added_.clear();
    TCA8418KeyboardReader::update();
    for (const auto& p : sim_) {
        if (std::find(_key_list.begin(), _key_list.end(), p) == _key_list.end()) {
            _key_list.push_back(p);
            added_.push_back(p);
        }
    }
}

SimKeyboardReader& install_sim_keyboard() {
    auto r = std::make_unique<SimKeyboardReader>();
    g_reader = r.get();
    M5Cardputer.Keyboard.begin(std::move(r));
    return *g_reader;
}

void update_inputs() {
    M5.update();
    M5Cardputer.Keyboard.updateKeyList();
    M5Cardputer.Keyboard.updateKeysState();
}

void set_sim_keys(const std::vector<Point2D_t>& keys) {
    if (g_reader) g_reader->set_keys(keys);
}

}  // namespace ui
}  // namespace cv

#endif

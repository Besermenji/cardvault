// Test builds only: a keyboard reader that adds simulated key positions to the
// real TCA8418 key list. The simulated keys go through the library's own
// key list, key state (fn/shift/ctrl/alt) and keymap, then our make_key and
// KeyStream, so only the I2C read and the switches are left out.
#pragma once

#ifdef CV_TEST_HOOKS

#include <M5Cardputer.h>
#include <utility/Keyboard/KeyboardReader/TCA8418.h>

#include <vector>

namespace cv {
namespace ui {

class SimKeyboardReader : public TCA8418KeyboardReader {
public:
    void update() override;
    // Replaces the set of simulated held keys (matrix x = column, y = row).
    void set_keys(const std::vector<Point2D_t>& keys) { sim_ = keys; }

private:
    std::vector<Point2D_t> sim_;
    std::vector<Point2D_t> added_;
};

// Installs a SimKeyboardReader as the keyboard (instead of the library's
// default reader) and returns it.
SimKeyboardReader& install_sim_keyboard();
// Polls the buttons and the keyboard: M5Cardputer.update() for builds that
// use install_sim_keyboard().
void update_inputs();
// Sets the simulated held keys; no-op before install_sim_keyboard().
void set_sim_keys(const std::vector<Point2D_t>& keys);

}  // namespace ui
}  // namespace cv

#endif

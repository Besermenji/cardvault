// Test builds only: simulated held keys. After M5Cardputer.update() the
// positions are added to the library's real key list and the key state is
// computed again, so fn/shift/ctrl state, the library keymap, our key mapping,
// rolls and auto-repeat all run as for a physical key. Only the I2C read and
// the switches are left out. Release builds get an empty function.
#pragma once

#ifdef CV_TEST_HOOKS

#include <M5Cardputer.h>

#include <vector>

namespace cv {
namespace ui {

// Replaces the set of simulated held keys (matrix x = column, y = row).
void set_sim_keys(const std::vector<Point2D_t>& keys);
// Call right after M5Cardputer.update().
void apply_sim_keys();

}  // namespace ui
}  // namespace cv

#else

namespace cv {
namespace ui {
inline void apply_sim_keys() {}
}  // namespace ui
}  // namespace cv

#endif

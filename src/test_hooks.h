// End-to-end test hooks: a line-based command protocol over USB serial that
// lets tools/e2e/run.py drive the real UI. Compiled only with CV_TEST_HOOKS
// (env:cardputer-e2e); CI verifies release images contain none of it.
#pragma once

#ifdef CV_TEST_HOOKS

#include "ui/app.h"

namespace cv {
namespace test {

void begin();
// Processes pending serial commands. Called once per main-loop iteration.
void poll(ui::App& a);
// True once after a simulated G0 press.
bool take_g0();

}  // namespace test
}  // namespace cv

#endif

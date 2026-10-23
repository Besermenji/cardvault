// User-facing texts that need to fit the 240x135 dialog (33 columns, 6 lines)
// live here so host tests can check their layout.
#pragma once

namespace cv {
namespace msg {

constexpr const char* kStorageSetupTitle = "Set up storage";
constexpr const char* kStorageSetup =
    "CardVault's own storage area needs setting up. Other firmware on this device is "
    "NOT affected. Press Y to set it up (this erases that area) or N to skip.";

constexpr int kDialogCols = 33;
constexpr int kDialogMaxLines = 6;

}  // namespace msg
}  // namespace cv

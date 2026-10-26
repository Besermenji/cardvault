// User-facing texts that need to fit the 240x135 dialog (33 columns, 6 lines)
// live here so host tests can check their layout.
#pragma once

namespace cv {
namespace msg {

constexpr const char* kStorageSetupTitle = "Set up storage";
constexpr const char* kStorageSetup =
    "CardVault's own storage area needs setting up. Other firmware on this device is "
    "NOT affected. Press Y to set it up (this erases that area) or N to skip.";

constexpr const char* kWipeResume = "Attempt limit reached or an erase was interrupted.";
constexpr const char* kWipeErasedTitle = "Vault erased";
constexpr const char* kWipeFailedTitle = "Erase failed";
constexpr const char* kWipeCounterStuck =
    "The attempt counter could not be reset. Power off and on to finish.";
constexpr const char* kWipeRetry = "Power the device off and on to retry.";
constexpr const char* kWipeNotArmed =
    "Your data may still be on the device. Power off and on, then erase it again.";
constexpr const char* kWipeRemount = "Storage will be set up again after the restart.";
constexpr const char* kCounterNotReset = "Attempt counter not reset";

constexpr int kDialogCols = 33;
constexpr int kDialogMaxLines = 6;

}  // namespace msg
}  // namespace cv

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

constexpr const char* kGuardUnavailableTitle = "Counter unavailable";
constexpr const char* kGuardUnavailable =
    "The wrong-passphrase counter cannot be opened, so unlocking is disabled. "
    "Power off and on. If this persists, reflash.";
constexpr const char* kVaultUnreadable = "The vault file is unreadable. Restore a backup from SD (TAB menu).";
constexpr const char* kVaultUnsupported =
    "This vault uses settings this firmware cannot read. Update the firmware; if that does not "
    "help, restore a backup.";
constexpr const char* kVaultNewer =
    "This vault was made by a newer CardVault. Update the firmware to open it.";
constexpr const char* kPayloadDamaged =
    "The passphrase is correct but the vault data is corrupted. Restore a backup from SD (TAB menu).";

constexpr int kDialogCols = 33;
constexpr int kDialogMaxLines = 6;

}  // namespace msg
}  // namespace cv

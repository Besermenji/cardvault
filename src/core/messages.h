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

constexpr const char* kCompactTitle = "Compact vault";
constexpr const char* kCompactConfirm =
    "Saves a safety copy to SD, erases old encrypted copies in flash, then rewrites your "
    "vault. Keep the power on. Y to start.";
constexpr const char* kCompactDone = "Old copies are gone. Only the current vault is in flash.";
// %s = backup file name
constexpr const char* kCompactDoneWithCopy =
    "Old copies are gone. Safety copy on SD: %s (opens with your vault passphrase).";
constexpr const char* kCompactNeedsSd =
    "Compact first saves a safety copy to SD. Insert a FAT32 microSD card and try again. "
    "Nothing was changed.";
constexpr const char* kCompactFailedTitle = "Compact failed";
constexpr const char* kCompactFailed =
    "Your vault is only in memory now and the device will not lock until it is saved. "
    "Y to retry. The SD safety copy can restore it.";
constexpr const char* kCompactAfterPassTitle = "Passphrase changed";
constexpr const char* kCompactAfterPass =
    "Flash still holds old copies that open with the OLD passphrase. Compact now? "
    "Old SD backups keep their own passphrase.";

constexpr const char* kTuning = "Tuning security...";
constexpr const char* kTuneFailedTitle = "Setup failed";
constexpr const char* kTuneFailed =
    "Not enough free memory to set up encryption. Restart the device and try again.";
constexpr const char* kTuneMissedTitle = "Unlock time";
// %u.%u = seconds with one decimal.
constexpr const char* kTuneMissed =
    "On this device an unlock takes about %u.%u s instead of the usual 2 s. Your data "
    "is protected either way.";

constexpr const char* kSdStalledTitle = "SD card not readable";
constexpr const char* kSdStalled =
    "The card did not respond in time. Try again; if it keeps failing, use another card or "
    "restart the device.";

constexpr const char* kCompactAfterRestoreTitle = "Restored";
constexpr const char* kCompactAfterRestore =
    "Flash still holds old copies of the replaced vault, which open with its passphrase. "
    "Compact now?";

// Backup with a saved Password entry (#84). Entry titles in these texts are
// cut to kEntryTitleCols so the longest still fits the dialog.
constexpr int kEntryTitleCols = 16;
constexpr const char* kPassSourceTitle = "Backup passphrase";
constexpr const char* kUseSavedPass = "Use saved password";
constexpr const char* kTypeNewPass = "Type a passphrase";
constexpr const char* kTypeOldPass = "Type the passphrase";
constexpr const char* kChoosePassTitle = "Choose password";
constexpr const char* kTooShort = "too short";
constexpr const char* kNoSavedPassTitle = "No saved passwords";
constexpr const char* kNoSavedPass =
    "The vault has no Password entries. Add one, or choose \"Type a passphrase\".";
constexpr const char* kNoSavedPassRestore =
    "The vault has no Password entries. Choose \"Type the passphrase\".";
// %s = entry title
constexpr const char* kSavedPassConfirm =
    "The backup will open with the password of \"%s\". That password is inside the backup too: "
    "keep a copy off this device. Y to back up.";
// %s = backup file name
constexpr const char* kBackupSavedTyped =
    "Saved /cardvault/%s (verified). You need the backup passphrase to restore it.";
// %s = backup file name, %s = entry title
constexpr const char* kBackupSavedEntry =
    "Saved /cardvault/%s (verified). It opens with the password of \"%s\".";

constexpr int kDialogCols = 33;
constexpr int kDialogMaxLines = 6;

}  // namespace msg
}  // namespace cv

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
constexpr const char* kTooLong = "too long";
constexpr const char* kOnlySpaces = "only spaces";
constexpr const char* kEntryNotFound = "Password entry not found";
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

// Update firmware from the SD card (#85). Every message says what to do next;
// docs/UPDATING.md has a table with the same texts.
constexpr const char* kUpdateTitle = "Update firmware";
constexpr const char* kUpdateNoCard = "Insert the microSD card with the update file.";
constexpr const char* kUpdateNoFileTitle = "No update file";
constexpr const char* kUpdateNoFile =
    "Copy cardvault-vX.Y.Z-cardputer-adv.cvu from the GitHub release into /cardvault on the "
    "SD card.";
constexpr const char* kUpdateLauncherTitle = "Not available";
constexpr const char* kUpdateLauncher =
    "CardVault was installed through Launcher or with another partition table. Update it from "
    "Launcher.";
constexpr const char* kUpdateNoSlotTitle = "Full install needed";
constexpr const char* kUpdateNoSlot =
    "Install this version once with the web flasher or M5Burner. Your vault is kept. After "
    "that, SD updates work.";
constexpr const char* kUpdateInMemory =
    "Your vault is only in memory right now. Fix storage (Compact vault) before updating.";
constexpr const char* kUpdateFailedTitle = "Update failed";
constexpr const char* kUpdateBadFile =
    "This is not a CardVault update file, or it is damaged. Download it again.";
constexpr const char* kUpdateBadSignature =
    "Not an official CardVault file. Download it only from the GitHub releases page.";
constexpr const char* kUpdateWrongDevice = "This update is for another device.";
constexpr const char* kUpdateTooBig = "This update does not fit this device's firmware slot.";
constexpr const char* kUpdateOlderTitle = "Older version";
// %s = file version, %s = running version
constexpr const char* kUpdateTooOld =
    "v%s is older than this version (v%s). To go back, use the web flasher.";
constexpr const char* kUpdateTruncated = "The file is incomplete. Copy it to the card again.";
constexpr const char* kUpdateDamaged = "The file is damaged. Copy it to the card again.";
constexpr const char* kUpdateReadFailed =
    "The card could not be read. Nothing was changed. Check the card and try again.";
constexpr const char* kUpdateWriteFailed =
    "Writing the update failed. Nothing was changed: this version stays installed.";
// %s = file version, %s = running version
constexpr const char* kUpdateConfirm =
    "Install v%s? Now v%s. Your vault is not changed. Keep the power on; if it is cut, this "
    "version still starts. Y to install.";
constexpr const char* kUpdateInstalling = "Installing...";
constexpr const char* kUpdateDoneTitle = "Updated";
// %s = new version
constexpr const char* kUpdateDone = "CardVault v%s is installed. Unlock as usual.";
constexpr const char* kUpdateRolledBackTitle = "Update rolled back";
// %s = version that did not start, %s = running version
constexpr const char* kUpdateRolledBack =
    "v%s did not start, so CardVault went back to v%s. Your vault is not changed.";

constexpr int kDialogCols = 33;
constexpr int kDialogMaxLines = 6;

}  // namespace msg
}  // namespace cv

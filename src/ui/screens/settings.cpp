// Settings, passphrase change, SD backup/restore and erase.
#include <M5Cardputer.h>
#include <esp_heap_caps.h>

#include "core/backup_key.h"
#include "core/flows.h"
#include "core/messages.h"
#include "platform/sd_backup.h"
#include "ui/app.h"
#include "vault/serializer.h"
#include "version.h"

namespace cv {
namespace ui {

namespace {

const uint32_t kAutolockChoices[] = {30, 60, 120, 300, 600, 1800};

void format_duration(uint32_t s, char* buf, size_t n) {
    if (s < 60)
        snprintf(buf, n, "%us", unsigned(s));
    else
        snprintf(buf, n, "%u min", unsigned(s / 60));
}

// SD failures: a card that stalled the driver gets its own message, so it is
// not reported as missing or as a generic read/write error.
void show_sd_error(const char* title, const char* text, uint16_t col = color::bad) {
    if (sd::wedged())
        app().router.push(make_message(msg::kSdStalledTitle, msg::kSdStalled, color::bad));
    else
        app().router.push(make_message(title, text, col));
}

void show_no_card(const char* hint) { show_sd_error("No SD card", hint, color::warn); }

// Rewrites the vault so flash holds only the current image. A verified copy
// of the same image goes to SD first, so a power cut during the erase cannot
// lose the vault. A storage failure leaves the vault only in memory: the
// device then refuses to lock and offers a retry (no second SD copy needed).
void run_compact(bool safety_copy) {
    App& a = app();
    std::string name;
    SafetyCopy copy;
    if (safety_copy) {
        copy = [&name](const std::vector<uint8_t>& image) {
            bool ok = sd::write_backup(image, name);
            sd::unmount();
            return ok;
        };
    }
    a.busy(safety_copy ? "Saving safety copy to SD..." : "Compacting...");
    switch (compact_vault(a.session, a.store, copy, a.flash_missing)) {
        case CompactResult::Done: {
            char text[140];
            if (safety_copy)
                snprintf(text, sizeof text, msg::kCompactDoneWithCopy, name.c_str());
            else
                snprintf(text, sizeof text, "%s", msg::kCompactDone);
            a.router.push(make_message(msg::kCompactTitle, text, color::good));
            break;
        }
        case CompactResult::NoSafetyCopy:
            show_sd_error(msg::kCompactTitle, msg::kCompactNeedsSd, color::warn);
            break;
        case CompactResult::StorageFailed:
            a.router.push(make_confirm(msg::kCompactFailedTitle, msg::kCompactFailed,
                                       [] { run_compact(false); }, color::bad));
            break;
        default:
            a.toast("Could not compact", color::bad, 3000);
            break;
    }
}

void compact_flow() {
    app().router.push(
        make_confirm(msg::kCompactTitle, msg::kCompactConfirm, [] { run_compact(true); }));
}

void change_passphrase_flow() {
    if (attempt_delay_active()) return;
    InputOpts o = existing_passphrase_opts("First confirm your current passphrase");
    app().router.push(make_text_input("Current passphrase", "", o, [](SString& current) {
        App& a = app();
        if (attempt_delay_active()) return;
        a.busy("Checking...");
        // Counts against the guard like the unlock screen, so an unlocked
        // device is no unlimited oracle for its own passphrase.
        VaultSession check;
        UnlockResult r = unlock_attempt(a.guard, check, a.store, current);
        check.close();
        switch (r.kind) {
            case UnlockResult::Unlocked:
                break;
            case UnlockResult::MustWipe:
                a.wipe_and_reboot("10 wrong passphrases in a row. All data is being erased.");
            case UnlockResult::Wrong: {
                a.arm_attempt_delay();
                char msg[48];
                uint32_t left = a.guard.remaining();
                snprintf(msg, sizeof msg, "Wrong passphrase (%u left)", unsigned(left));
                a.toast(msg, color::bad, 2500);
                return;
            }
            case UnlockResult::Damaged:
                a.toast(vault_error_str(r.error), color::bad, 2500);
                return;
            case UnlockResult::Refused:
                a.toast("Storage error", color::bad, 2500);
                return;
            case UnlockResult::Failed:
                if (r.retry_delay_ms) a.arm_attempt_delay();  // counted: no free retries
                a.toast(vault_error_str(r.error), color::bad, 2500);
                return;
            case UnlockResult::Empty:
                return;
        }
        ask_new_passphrase("New passphrase", [](SString& next) {
            App& a = app();
            // Re-tune: vaults from older builds get the full unlock cost too.
            // Never with less memory than the vault already has: the unlocked
            // vault takes heap, which would otherwise shrink the new size.
            KdfTuning tuning;
            if (!tune_security(tuning, a.session.header().kdf.mem_kib)) return;
            a.busy("Re-encrypting...");
            VaultError e =
                a.session.change_passphrase(a.store, next, tuning.params, kdf_closed_loop(tuning));
            if (e == VaultError::Ok) {
                a.router.push(make_confirm(msg::kCompactAfterPassTitle, msg::kCompactAfterPass,
                                           [] { run_compact(true); }, color::good));
                show_tune_warning(tuning);  // on top, read first
            } else {
                a.toast(vault_error_str(e), color::bad, 3000);
            }
        });
    }));
}

void erase_flow() {
    InputOpts o;
    o.confirm_discard = false;
    o.hint = "Type ERASE to destroy all data";
    app().router.push(make_text_input("Erase vault", "", o, [](SString& s) {
        if (s == "ERASE") app().wipe_and_reboot("Erasing on request.");
        app().toast("Not erased", color::warn);
    }));
}

void show_about() {
    App& a = app();
    char msg[240];
    const KdfParams& k = a.session.header().kdf;
    unsigned used = unsigned(serialized_size(a.session.data()));
    snprintf(msg, sizeof msg,
             "v%s  |  %u secrets  |  %u.%u of %u KB used  |  Argon2id %u KiB x%u  |  heap %u KB "
             "free  |  MIT licensed",
             CV_VERSION, unsigned(a.session.data().entries.size()), used / 1024,
             used % 1024 * 10 / 1024, unsigned(kMaxPayload / 1024), unsigned(k.mem_kib),
             unsigned(k.passes), unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    a.router.push(make_message("CardVault", msg, color::accent));
}

class SettingsScreen : public Screen {
public:
    const char* name() const override { return "settings"; }
#ifdef CV_TEST_HOOKS
    void debug(std::string& out) const override { out = "sel=" + std::to_string(sel_); }
#endif
    void on_key(const Key& k) override {
        invalidate();
        if (k.back()) {
            app().router.pop();
        } else if (k.up()) {
            sel_ = (sel_ + kRows - 1) % kRows;
        } else if (k.down()) {
            sel_ = (sel_ + 1) % kRows;
        } else if (k.left() || k.right()) {
            adjust(k.right() ? 1 : -1);
        } else if (k.is(KeyCode::Enter)) {
            activate();
        }
    }

    void draw(Canvas& c) override {
        App& a = app();
        clear(c);
        header(c, "Settings");
        const int row_h = 15;
        int visible = kBodyH / row_h;
        if (sel_ < first_) first_ = sel_;
        if (sel_ >= first_ + visible) first_ = sel_ - visible + 1;
        static const char* labels[kRows] = {"Auto-lock",       "Brightness",     "Set clock (UTC)",
                                            "Change passphrase", "Backup to SD", "Restore from SD",
                                            "About",           "Compact vault",  "Erase vault"};
        for (int i = 0; i < visible && first_ + i < kRows; i++) {
            int idx = first_ + i;
            int y = kBodyY + i * row_h;
            list_row(c, y, row_h - 1, idx == sel_);
            text_small(c, 10, y + 3, labels[idx], idx == kRows - 1 ? color::bad : color::text);
            char val[24] = "";
            if (idx == 0) format_duration(a.settings.autolock_s, val, sizeof val);
            if (idx == 1) snprintf(val, sizeof val, "%u%%", unsigned(a.settings.brightness * 100 / 255));
            if (idx == 2 && device::time_valid()) snprintf(val, sizeof val, "set");
            if (idx == 2 && !device::time_valid()) snprintf(val, sizeof val, "not set");
            if (val[0]) {
                c.setTextDatum(top_right);
                text_small(c, kW - 12, y + 3, idx < 2 ? (SString("< ") + val + " >").c_str() : val,
                           color::accent);
                c.setTextDatum(top_left);
            }
        }
        scrollbar(c, first_, visible, kRows);
        footer(c, "ENTER select  ,/ change  ` back");
    }

private:
    static constexpr int kRows = 9;

    void adjust(int d) {
        App& a = app();
        if (sel_ == 0) {
            const int n = sizeof kAutolockChoices / sizeof kAutolockChoices[0];
            int i = 0;
            while (i < n - 1 && kAutolockChoices[i] < a.settings.autolock_s) i++;
            i = constrain(i + d, 0, n - 1);
            a.settings.autolock_s = kAutolockChoices[i];
        } else if (sel_ == 1) {
            // Signed sum: 20 - 25 must clamp to the minimum, not wrap around.
            int v = int(a.settings.brightness) + d * 25;
            a.settings.brightness =
                uint32_t(constrain(v, int(kMinBrightness), int(kMaxBrightness)));
            a.apply_brightness();
        } else {
            return;
        }
        device::save_settings(a.settings);
    }

    void activate() {
        switch (sel_) {
            case 0:
            case 1: adjust(1); break;
            case 2: ask_set_time(); break;
            case 3: change_passphrase_flow(); break;
            case 4: start_backup_flow(); break;
            case 5: start_restore_flow(); break;
            case 6: show_about(); break;
            case 7: compact_flow(); break;
            case 8: erase_flow(); break;
        }
    }

    int sel_ = 0;
    int first_ = 0;
};

using SavedPassUse = std::function<void(uint32_t id, const SString& title)>;

const char* problem_label(BackupKeyProblem p) {
    switch (p) {
        case BackupKeyProblem::Short: return msg::kTooShort;
        case BackupKeyProblem::Long: return msg::kTooLong;
        case BackupKeyProblem::Blank: return msg::kOnlySpaces;
        case BackupKeyProblem::None: break;
    }
    return "";
}

// Lists the vault's Password entries to pick one as a backup passphrase (#84).
// For a new backup, entries that break the passphrase policy show why and
// picking one asks again, on the same row; for a restore, restore_check
// decides. `back` reopens the menu this came from (Esc, or no entries), so
// the choice to type the passphrase stays one key away.
void choose_saved_password(bool for_backup, SavedPassUse use, std::function<void()> back,
                           int sel = 0) {
    App& a = app();
    auto choices = backup_key_choices(a.session.data());
    if (choices.empty()) {
        a.router.push(make_message(msg::kNoSavedPassTitle,
                                   for_backup ? msg::kNoSavedPass : msg::kNoSavedPassRestore,
                                   color::warn, back));
        return;
    }
    std::vector<MenuItem> items;
    for (size_t i = 0; i < choices.size(); i++) {
        const BackupKeyChoice& c = choices[i];
        bool usable = c.usable() || !for_backup;
        uint32_t id = c.id;
        SString title = c.title;
        int row = int(i);
        std::function<void()> action;
        if (usable) {
            action = [use, id, title] { use(id, title); };
        } else {
            action = [use, back, row] {
                app().toast(vault_error_str(VaultError::WeakPassphrase), color::warn, 2500);
                choose_saved_password(true, use, back, row);
            };
        }
        items.push_back({c.title, action, usable ? "" : problem_label(c.problem),
                         usable ? color::text : color::dim});
    }
    a.router.push(make_menu(msg::kChoosePassTitle, std::move(items), sel, back));
}

// Copies the chosen entry's password into `pass`; on failure says why.
bool saved_password(uint32_t id, bool for_backup, SString& pass) {
    switch (backup_key_from_entry(app().session.data(), id, for_backup, pass)) {
        case BackupKey::Ok:
            return true;
        case BackupKey::Weak:
            app().toast(vault_error_str(for_backup ? VaultError::WeakPassphrase
                                                   : VaultError::EmptyPassphrase),
                        color::warn, 2500);
            return false;
        case BackupKey::Missing:
            break;
    }
    app().toast(msg::kEntryNotFound, color::bad, 2500);
    return false;
}

// "3 saved" / "none": how many Password entries the picker would list.
SString saved_count() {
    size_t n = 0;
    for (const auto& e : app().session.data().entries)
        if (e.type == EntryType::Password) n++;
    if (n == 0) return "none";
    char buf[16];
    snprintf(buf, sizeof buf, "%u saved", unsigned(n));
    return buf;
}

// Restore guesses count against the guard while a locked vault exists. On a
// device without a vault (setup screen) there is nothing to protect, and a
// missing counter would otherwise allow a single guess.
bool restore_guarded() { return !app().session.is_open() && app().store.exists(); }

void restore_with(std::shared_ptr<std::vector<uint8_t>> image, SString& pass) {
    App& a = app();
    bool locked = restore_guarded();
    if (locked && attempt_delay_active()) return;
    a.busy("Decrypting backup...");
    RestoreCheck r = restore_check(a.guard, locked, *image, pass);
    switch (r.kind) {
        case RestoreCheck::MustWipe:
            a.wipe_and_reboot("10 wrong passphrases in a row. All data is being erased.");
        case RestoreCheck::Refused:
            a.toast("Storage error", color::bad);
            return;
        case RestoreCheck::Wrong:
            if (locked) a.arm_attempt_delay();
            a.router.push(make_message("Restore failed", vault_error_str(r.error), color::bad));
            return;
        case RestoreCheck::Failed:
            if (locked && r.retry_delay_ms) a.arm_attempt_delay();
            a.router.push(make_message("Restore failed", vault_error_str(r.error), color::bad));
            return;
        case RestoreCheck::Empty:
            return;
        case RestoreCheck::Ok:
            break;
    }
    auto box = std::make_shared<SecretBox>(pass);
    char msg[120];
    snprintf(msg, sizeof msg,
             "Replace the vault on this device with the backup (%u secrets)? The current "
             "vault will be overwritten.",
             unsigned(r.entries));
    a.router.push(make_confirm("Replace vault?", msg, [image, box] {
        App& a = app();
        a.busy("Restoring...");
        VaultError e = restore_commit(a.guard, a.session, a.store, *image, box->value);
        if (e != VaultError::Ok) {
            a.lock();
            a.toast(vault_error_str(e), color::bad, 3000);
            return;
        }
        a.router.reset(make_home());
        // The replaced vault is still in flash and opens with its old passphrase.
        a.router.push(make_confirm(msg::kCompactAfterRestoreTitle, msg::kCompactAfterRestore,
                                   [] { run_compact(true); }, color::good));
        if (a.guard.failures() > 0)
            a.toast(msg::kCounterNotReset, color::warn, 3000);
        else
            a.toast("Backup restored");
    }, color::bad));
}

void ask_restore_passphrase(std::shared_ptr<std::vector<uint8_t>> image) {
    InputOpts o = existing_passphrase_opts("Passphrase used when this backup was made");
    app().router.push(make_text_input("Backup passphrase", "", o,
                                      [image](SString& pass) { restore_with(image, pass); }));
}

// While unlocked: pick a saved password or type the passphrase for `image`.
void push_restore_source(std::shared_ptr<std::vector<uint8_t>> image) {
    std::vector<MenuItem> items;
    items.push_back({msg::kUseSavedPass,
                     [image] {
                         choose_saved_password(
                             false,
                             [image](uint32_t id, const SString&) {
                                 SString pass;
                                 if (saved_password(id, false, pass)) restore_with(image, pass);
                             },
                             [image] { push_restore_source(image); });
                     },
                     saved_count()});
    items.push_back({msg::kTypeOldPass, [image] { ask_restore_passphrase(image); }});
    app().router.push(make_menu(msg::kPassSourceTitle, std::move(items)));
}

void restore_from(const std::string& name) {
    auto image = std::make_shared<std::vector<uint8_t>>();
    bool read_ok = sd::read_backup(name, *image);
    sd::unmount();  // the image is in RAM; the card is not needed any more
    if (!read_ok) {
        show_sd_error("Restore", "Could not read the backup file.");
        return;
    }
    // restore_check() validates the file (before any attempt is counted).
    // While locked, restore guesses share the unlock screen's delay.
    if (restore_guarded() && attempt_delay_active()) return;
    if (app().session.is_open())
        push_restore_source(image);
    else
        ask_restore_passphrase(image);  // locked or setup: no entries to pick from
}

// Encrypts the open vault under `pass`, writes it to SD and reports. `entry_title`
// names the saved password used (already cut to fit), or is null when typed.
void write_backup_with(const SString& pass, const char* entry_title) {
    App& a = app();
    a.busy("Encrypting backup...");
    std::vector<uint8_t> image;
    VaultError e = a.session.export_image(pass, a.session.header().kdf, image);
    if (e != VaultError::Ok) {
        a.router.push(make_message("Backup failed", vault_error_str(e), color::bad));
        return;
    }
    a.busy("Writing to SD...");
    std::string name;
    bool ok = sd::write_backup(image, name);
    sd::unmount();
    if (!ok) {
        show_sd_error("Backup failed", "Could not write to the SD card.");
        return;
    }
    char msg[160];
    if (entry_title)
        snprintf(msg, sizeof msg, msg::kBackupSavedEntry, name.c_str(), entry_title);
    else
        snprintf(msg, sizeof msg, msg::kBackupSavedTyped, name.c_str());
    a.router.push(make_message("Backup saved", msg, color::good));
}

// Pick a saved password or type a new passphrase; `hold` keeps the card
// mounted until the backup is written or the flow is left.
void push_backup_source(std::shared_ptr<sd::MountHold> hold) {
    std::vector<MenuItem> items;
    items.push_back({msg::kUseSavedPass, [hold] {
                         choose_saved_password(
                             true,
                             [hold](uint32_t id, const SString& title) {
                                 auto shown = std::make_shared<SString>(fit(title, msg::kEntryTitleCols));
                                 char text[200];
                                 snprintf(text, sizeof text, msg::kSavedPassConfirm, shown->c_str());
                                 app().router.push(make_confirm(msg::kUseSavedPass, text, [hold, id, shown] {
                                     // Looked up only now, so the copy lives just as long as the backup.
                                     SString pass;
                                     if (saved_password(id, true, pass)) write_backup_with(pass, shown->c_str());
                                 }));
                             },
                             [hold] { push_backup_source(hold); });
                     },
                     saved_count()});
    items.push_back({msg::kTypeNewPass, [hold] {
                         ask_new_passphrase("Backup passphrase",
                                            [hold](SString& pass) { write_backup_with(pass, nullptr); });
                     }});
    app().router.push(make_menu(msg::kPassSourceTitle, std::move(items)));
}

}  // namespace

ScreenPtr make_settings() { return ScreenPtr(new SettingsScreen()); }

void start_backup_flow() {
    if (!app().session.is_open()) return;
    app().busy("Checking SD card...");
    if (!sd::mount()) {
        show_no_card("Insert a FAT32 microSD card and try again.");
        return;
    }
    // Stays mounted through the menus and prompts; released when the backup
    // completes or is cancelled (or the device locks).
    push_backup_source(std::make_shared<sd::MountHold>());
}

void start_restore_flow() {
    app().busy("Reading SD card...");
    if (!sd::mount()) {
        show_no_card("Insert the microSD card with your backup.");
        return;
    }
    auto names = sd::list_backups();
    if (names.empty()) {
        sd::unmount();
        app().router.push(make_message("No backups", "No .cvb files found in /cardvault on the SD card.",
                                       color::warn));
        return;
    }
    // Stays mounted while the menu is open; reading a backup unmounts, and
    // leaving the menu (or locking) releases it.
    auto hold = std::make_shared<sd::MountHold>();
    std::vector<MenuItem> items;
    for (auto& n : names) {
        std::string name = n;
        items.push_back({SString(name.c_str()), [name, hold] { restore_from(name); }});
    }
    app().router.push(make_menu("Choose backup", std::move(items)));
}

}  // namespace ui
}  // namespace cv

// Settings, passphrase change, SD backup/restore and erase.
#include <M5Cardputer.h>
#include <esp_heap_caps.h>

#include "core/flows.h"
#include "platform/sd_backup.h"
#include "ui/app.h"
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
                a.toast(vault_error_str(VaultError::Corrupt), color::bad, 2500);
                return;
            case UnlockResult::Refused:
                a.toast("Storage error", color::bad, 2500);
                return;
            case UnlockResult::Failed:
                a.toast(vault_error_str(r.error), color::bad, 2500);
                return;
            case UnlockResult::Empty:
                return;
        }
        ask_new_passphrase("New passphrase", [](SString& next) {
            App& a = app();
            a.busy("Re-encrypting...");
            VaultError e = a.session.change_passphrase(a.store, next, a.session.header().kdf);
            if (e == VaultError::Ok)
                a.router.push(make_message("Passphrase changed",
                                           "Old SD backups still open with the passphrase they "
                                           "were made with.",
                                           color::good));
            else
                a.toast(vault_error_str(e), color::bad, 3000);
        });
    }));
}

void erase_flow() {
    InputOpts o;
    o.hint = "Type ERASE to destroy all data";
    app().router.push(make_text_input("Erase vault", "", o, [](SString& s) {
        if (s == "ERASE") app().wipe_and_reboot("Erasing on request.");
        app().toast("Not erased", color::warn);
    }));
}

void show_about() {
    App& a = app();
    char msg[200];
    const KdfParams& k = a.session.header().kdf;
    snprintf(msg, sizeof msg,
             "v%s  |  %u secrets  |  Argon2id %u KiB x%u  |  heap %u KB free  |  MIT licensed",
             CV_VERSION, unsigned(a.session.data().entries.size()), unsigned(k.mem_kib),
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
                                            "About",           "Erase vault"};
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
    static constexpr int kRows = 8;

    void adjust(int d) {
        App& a = app();
        if (sel_ == 0) {
            const int n = sizeof kAutolockChoices / sizeof kAutolockChoices[0];
            int i = 0;
            while (i < n - 1 && kAutolockChoices[i] < a.settings.autolock_s) i++;
            i = constrain(i + d, 0, n - 1);
            a.settings.autolock_s = kAutolockChoices[i];
        } else if (sel_ == 1) {
            a.settings.brightness = uint32_t(constrain(int(a.settings.brightness) + d * 25, 20, 255));
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
            case 7: erase_flow(); break;
        }
    }

    int sel_ = 0;
    int first_ = 0;
};

void restore_from(const std::string& name) {
    auto image = std::make_shared<std::vector<uint8_t>>();
    if (!sd::read_backup(name, *image)) {
        app().router.push(make_message("Restore", "Could not read the backup file.", color::bad));
        return;
    }
    Header h;
    VaultError e = VaultSession::peek_header(*image, h);
    if (e != VaultError::Ok) {
        app().router.push(make_message("Restore", vault_error_str(e), color::bad));
        return;
    }
    // While locked, restore guesses share the unlock screen's delay.
    if (!app().session.is_open() && attempt_delay_active()) return;
    InputOpts o = existing_passphrase_opts("Passphrase used when this backup was made");
    app().router.push(make_text_input("Backup passphrase", "", o, [image](SString& pass) {
        App& a = app();
        bool locked = !a.session.is_open();
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
            a.toast("Backup restored");
        }, color::bad));
    }));
}

}  // namespace

ScreenPtr make_settings() { return ScreenPtr(new SettingsScreen()); }

void start_backup_flow() {
    if (!app().session.is_open()) return;
    app().busy("Checking SD card...");
    if (!sd::mount()) {
        app().router.push(make_message("No SD card", "Insert a FAT32 microSD card and try again.",
                                       color::warn));
        return;
    }
    ask_new_passphrase("Backup passphrase", [](SString& pass) {
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
            a.router.push(make_message("Backup failed", "Could not write to the SD card.", color::bad));
            return;
        }
        char msg[140];
        snprintf(msg, sizeof msg,
                 "Saved /cardvault/%s (verified). You need the backup passphrase to restore it.",
                 name.c_str());
        a.router.push(make_message("Backup saved", msg, color::good));
    });
}

void start_restore_flow() {
    app().busy("Reading SD card...");
    if (!sd::mount()) {
        app().router.push(make_message("No SD card", "Insert the microSD card with your backup.",
                                       color::warn));
        return;
    }
    auto names = sd::list_backups();
    if (names.empty()) {
        sd::unmount();
        app().router.push(make_message("No backups", "No .cvb files found in /cardvault on the SD card.",
                                       color::warn));
        return;
    }
    std::vector<MenuItem> items;
    for (auto& n : names) {
        std::string name = n;
        items.push_back({SString(name.c_str()), [name] { restore_from(name); }});
    }
    app().router.push(make_menu("Choose backup", std::move(items)));
}

}  // namespace ui
}  // namespace cv

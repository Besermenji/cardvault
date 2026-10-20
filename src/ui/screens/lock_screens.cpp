// First-run setup and the unlock screen.
#include <M5Cardputer.h>

#include "core/flows.h"
#include "ui/app.h"

#ifndef CV_TEST_DELAY_DIV
#define CV_TEST_DELAY_DIV 1
#endif

namespace cv {
namespace ui {

namespace {

void draw_lock_icon(Canvas& c, int cx, int cy, uint16_t col) {
    c.fillRoundRect(cx - 11, cy - 2, 22, 17, 3, col);
    c.drawArc(cx, cy - 2, 8, 6, 180, 360, col);
    c.fillRect(cx - 8, cy - 4, 3, 3, col);
    c.fillRect(cx + 6, cy - 4, 3, 3, col);
    c.fillCircle(cx, cy + 5, 2, color::bg);
    c.fillRect(cx - 1, cy + 5, 2, 5, color::bg);
}

void create_vault(SString& pass) {
    App& a = app();
    a.busy("Tuning security...");
    KdfParams kdf = device::calibrate_kdf(2000);
    a.busy("Creating vault...");
    VaultError e = a.session.create(a.store, pass, kdf);
    if (e != VaultError::Ok) {
        a.router.push(make_message("Setup failed", vault_error_str(e), color::bad));
        return;
    }
    a.guard.reset();
    a.router.reset(make_home());
    a.toast("Vault created. TAB opens the menu.", color::good, 3000);
}

class SetupScreen : public Screen {
public:
    const char* name() const override { return "setup"; }
    void on_key(const Key& k) override {
        if (k.is(KeyCode::Enter)) {
            ask_new_passphrase("New passphrase", [](SString& p) { create_vault(p); });
        } else if (k.is(KeyCode::Tab)) {
            start_restore_flow();
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        header(c, "Welcome to CardVault");
        int y = kBodyY + 2;
        const char* lines[] = {
            "Offline vault for seed phrases,",
            "passwords, 2FA codes and notes.",
            "",
            "- Encrypted with your passphrase",
            "  (Argon2id + XChaCha20-Poly1305)",
            "- 10 wrong passphrases ERASE it",
            "- Keep a paper/SD backup!",
        };
        for (const char* l : lines) {
            text_small(c, 8, y, l, l[0] == '-' ? color::text : color::dim);
            y += 10;
        }
        c.fillRoundRect(8, y + 4, kW - 16, 18, 4, color::accent);
        c.setFont(CV_FONT_MONO);
        c.setTextColor(color::bg);
        c.setTextDatum(middle_center);
        c.drawString("ENTER  create a new vault", kW / 2, y + 13);
        c.setTextDatum(top_left);
        footer(c, "TAB restore from SD backup");
    }
};

class UnlockScreen : public Screen {
public:
    const char* name() const override { return "unlock"; }
    UnlockScreen() { schedule(); }
    ~UnlockScreen() override { wipe(pass_); }

    void on_key(const Key& k) override {
        invalidate();
        if (k.is(KeyCode::Tab)) {
            open_menu();
            return;
        }
        if (waiting()) return;
        switch (k.code) {
            case KeyCode::Enter: attempt(); return;
            case KeyCode::Backspace:
                if (!pass_.empty()) pass_.pop_back();
                return;
            case KeyCode::Opt: reveal_ = !reveal_; return;
            case KeyCode::Esc: wipe(pass_); return;
            case KeyCode::Char:
                if (pass_.size() < 128) pass_ += k.ch;
                error_ = nullptr;
                return;
            default: return;
        }
    }

    void tick(uint32_t now) override {
        uint32_t sec = now / 1000;
        if (waiting() && sec != last_sec_) {
            last_sec_ = sec;
            invalidate();
        }
        bool phase = (now / 500) % 2;
        if (phase != blink_) {
            blink_ = phase;
            invalidate();
        }
    }

    void draw(Canvas& c) override {
        App& a = app();
        clear(c);
        header(c, "CardVault");
        draw_lock_icon(c, 24, kBodyY + 18, waiting() ? color::bad : color::accent);
        c.setFont(CV_FONT_TITLE);
        c.setTextColor(color::text);
        c.drawString("Locked", 46, kBodyY + 8);

        uint32_t left = a.guard.remaining();
        char info[48];
        if (a.guard.failures() == 0) {
            snprintf(info, sizeof info, "Enter your passphrase");
        } else {
            snprintf(info, sizeof info, "%u attempt%s left before wipe", unsigned(left),
                     left == 1 ? "" : "s");
        }
        text_small(c, 46, kBodyY + 26, info, a.guard.failures() ? color::warn : color::dim);

        int by = kBodyY + 44;
        c.fillRoundRect(6, by, kW - 12, 22, 4, color::panel);
        c.drawRoundRect(6, by, kW - 12, 22, 4, waiting() ? color::line : color::accent);
        if (waiting()) {
            uint32_t ms = unlock_at_ - millis();
            char buf[40];
            unsigned secs = unsigned((ms + 999) / 1000);
            snprintf(buf, sizeof buf, "Wait %u:%02u", secs / 60, secs % 60);
            c.setFont(CV_FONT_MONO);
            c.setTextColor(color::bad);
            c.setTextDatum(middle_center);
            c.drawString(buf, kW / 2, by + 11);
            c.setTextDatum(top_left);
        } else {
            const int cols = (kW - 24) / kMonoW;
            size_t shown = pass_.size() > size_t(cols) ? pass_.size() - cols : 0;
            if (reveal_) {
                text_mono(c, 12, by + 3, pass_.c_str() + shown);
            } else {
                mask(c, 12, by + 3, pass_.size() - shown, cols);
            }
            if (blink_) {
                int cx = 12 + int(pass_.size() - shown) * kMonoW;
                c.fillRect(cx, by + 4, 2, 14, color::accent);
            }
        }
        if (error_) text_small(c, 8, by + 28, error_, color::bad);
        footer(c, "ENTER unlock  OPT show  TAB more");
    }

private:
    bool waiting() const { return int32_t(unlock_at_ - millis()) > 0; }

    void schedule() {
        // Test builds shorten the real schedule (verified separately by host
        // tests) so end-to-end runs don't take a quarter of an hour.
        uint32_t d = app().guard.required_delay_ms() / CV_TEST_DELAY_DIV;
        unlock_at_ = millis() + d;
    }

    void attempt() {
        App& a = app();
        if (pass_.empty()) return;
        a.busy("Unlocking...");
        UnlockResult r = unlock_attempt(a.guard, a.session, a.store, pass_);
        wipe(pass_);
        reveal_ = false;
        switch (r.kind) {
            case UnlockResult::Unlocked:
                a.router.reset(make_home());
                return;
            case UnlockResult::MustWipe:
                a.wipe_and_reboot("10 wrong passphrases in a row. All data is being erased.");
            case UnlockResult::Wrong:
                error_ = "Wrong passphrase";
                schedule();
                return;
            case UnlockResult::Damaged:
                a.router.push(make_message("Vault damaged",
                                           "The passphrase is correct but the vault data is "
                                           "corrupted. Restore a backup from SD (TAB menu).",
                                           color::bad));
                return;
            case UnlockResult::Refused:
                error_ = "Storage error - try again";
                return;
            case UnlockResult::Failed:
                error_ = vault_error_str(r.error);
                return;
        }
    }

    void open_menu() {
        std::vector<MenuItem> items;
        items.push_back({"Restore SD backup", [] { start_restore_flow(); }});
        items.push_back({"Erase vault", [] {
                             InputOpts o;
                             o.hint = "Type ERASE to destroy all data";
                             app().router.push(make_text_input(
                                 "Erase vault", "", o, [](SString& s) {
                                     if (s == "ERASE")
                                         app().wipe_and_reboot("Erasing on request.");
                                     app().toast("Not erased", color::warn);
                                 }));
                         },
                         "", color::bad});
        app().router.push(make_menu("Options", std::move(items)));
    }

    SString pass_;
    bool reveal_ = false;
    bool blink_ = false;
    uint32_t unlock_at_ = 0;
    uint32_t last_sec_ = 0;
    const char* error_ = nullptr;
};

}  // namespace

ScreenPtr make_setup() { return ScreenPtr(new SetupScreen()); }
ScreenPtr make_unlock() { return ScreenPtr(new UnlockScreen()); }

}  // namespace ui
}  // namespace cv

// Single-line and multi-line text editor, optionally masked.
#include <cmath>

#include "features/pwgen.h"
#include "ui/app.h"

namespace cv {
namespace ui {

namespace {

constexpr int kCols = (kW - 12) / kMonoW;  // 28

class TextInputScreen : public Screen {
public:
    const char* name() const override { return "text_input"; }
    const char* title() const override { return title_; }
#ifdef CV_TEST_HOOKS
    void debug(std::string& out) const override {
        out = "len=" + std::to_string(buf_.size()) + " reveal=" + std::to_string(reveal_);
    }
#endif
    TextInputScreen(const char* title, const SString& initial, InputOpts o, TextDone done)
        : title_(title), buf_(initial), cur_(initial.size()), o_(o), done_(std::move(done)) {}
    ~TextInputScreen() override { wipe(buf_); }

    void on_key(const Key& k) override {
        invalidate();
        switch (k.code) {
            case KeyCode::Esc: app().router.pop(); return;
            case KeyCode::Opt:
                if (o_.mask) reveal_ = !reveal_;
                return;
            case KeyCode::Tab:
                finish();
                return;
            case KeyCode::Enter:
                if (o_.multiline)
                    insert('\n');
                else
                    finish();
                return;
            case KeyCode::Backspace:
                if (cur_ > 0) {
                    buf_.erase(cur_ - 1, 1);
                    cur_--;
                    strength_dirty_ = true;
                }
                return;
            case KeyCode::Left:
                if (cur_ > 0) cur_--;
                return;
            case KeyCode::Right:
                if (cur_ < buf_.size()) cur_++;
                return;
            case KeyCode::Up: move_line(-1); return;
            case KeyCode::Down: move_line(1); return;
            case KeyCode::Char: insert(k.ch); return;
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        header(c, title_);
        int y0 = kBodyY + 2;
        int rows = o_.strength ? 3 : (kBodyH - 4) / kMonoH;
        if (o_.hint) {
            text_small(c, 6, y0, o_.hint);
            y0 += 11;
            rows = o_.strength ? 3 : (kBodyH - 15) / kMonoH;
        }
        int box_h = rows * kMonoH + 4;
        c.drawRoundRect(3, y0 - 2, kW - 6, box_h, 3, color::line);

        auto lines = wrap(buf_.data(), buf_.size(), kCols, true);
        int cl = 0;
        size_t ccol = 0;
        locate(lines, cl, ccol);
        if (cl < scroll_) scroll_ = cl;
        if (cl >= scroll_ + rows) scroll_ = cl - rows + 1;

        c.setFont(CV_FONT_MONO);
        for (int r = 0; r < rows && scroll_ + r < int(lines.size()); r++) {
            const Line& l = lines[scroll_ + r];
            int y = y0 + r * kMonoH;
            if (o_.mask && !reveal_) {
                mask(c, 6, y, l.len, kCols);
            } else {
                SString s(buf_.data() + l.start, l.len);
                text_mono(c, 6, y, s.c_str());
            }
        }
        // Cursor
        if ((millis() / 500) % 2 == 0) {
            int cx = 6 + int(ccol) * kMonoW;
            int cy = y0 + (cl - scroll_) * kMonoH;
            c.fillRect(cx, cy + 1, 2, kMonoH - 2, color::accent);
        }

        if (o_.strength) {
            // Recomputed only after an edit, not on every cursor blink.
            if (strength_dirty_) {
                strength_bits_ = passphrase_strength_bits(buf_);
                strength_dirty_ = false;
            }
            double bits = strength_bits_;
            const char* label = bits < 40 ? "weak" : bits < 60 ? "fair" : bits < 80 ? "good" : "strong";
            uint16_t col = bits < 40 ? color::bad : bits < 60 ? color::warn : color::good;
            int sy = y0 + box_h + 4;
            progress(c, 6, sy, kW - 80, 6, float(bits / 100.0), col);
            char buf[32];
            snprintf(buf, sizeof buf, "%s ~%d bits", label, int(bits));
            text_small(c, kW - 70, sy - 1, buf, col);
            char len[24];
            snprintf(len, sizeof len, "%u chars", unsigned(buf_.size()));
            text_small(c, 6, sy + 10, len);
        }
        footer(c, o_.multiline ? "TAB done  fn+` cancel  fn+;./, move"
                               : (o_.mask ? "ENTER ok  OPT show/hide  fn+` cancel"
                                          : "ENTER ok  fn+` cancel  fn+,/ move"));
    }

    void tick(uint32_t now) override {
        // Blink the cursor.
        bool phase = (now / 500) % 2;
        if (phase != blink_) {
            blink_ = phase;
            invalidate();
        }
    }

private:
    void insert(char ch) {
        if (buf_.size() >= o_.max_len) return;
        buf_.insert(buf_.begin() + cur_, ch);
        cur_++;
        strength_dirty_ = true;
    }

    void finish() {
        if (buf_.size() < o_.min_len) {
            char msg[40];
            snprintf(msg, sizeof msg, "At least %u character%s", unsigned(o_.min_len),
                     o_.min_len == 1 ? "" : "s");
            app().toast(msg, color::warn);
            return;
        }
        TextDone done = done_;
        SString value = buf_;
        app().router.pop();
        if (done) done(value);
        wipe(value);
    }

    void locate(const std::vector<Line>& lines, int& line, size_t& col) const {
        cursor_locate(lines, cur_, kCols, line, col);
    }

    void move_line(int delta) {
        auto lines = wrap(buf_.data(), buf_.size(), kCols, true);
        cur_ = cursor_move_line(lines, cur_, kCols, delta);
    }

    const char* title_;
    SString buf_;
    size_t cur_;
    InputOpts o_;
    TextDone done_;
    bool reveal_ = false;
    int scroll_ = 0;
    bool blink_ = false;
    bool strength_dirty_ = true;
    double strength_bits_ = 0;
};

class MenuScreen : public Screen {
public:
    const char* name() const override { return "menu"; }
    const char* title() const override { return title_; }
#ifdef CV_TEST_HOOKS
    void debug(std::string& out) const override {
        out = "sel=" + std::to_string(sel_) + " items=";
        for (size_t i = 0; i < items_.size(); i++) {
            if (i) out += "|";
            out += items_[i].label.c_str();
        }
    }
#endif
    MenuScreen(const char* title, std::vector<MenuItem> items)
        : title_(title), items_(std::move(items)) {}

    void on_key(const Key& k) override {
        invalidate();
        int n = int(items_.size());
        if (k.back()) {
            app().router.pop();
        } else if (k.up()) {
            sel_ = (sel_ + n - 1) % n;
        } else if (k.down()) {
            sel_ = (sel_ + 1) % n;
        } else if (k.is(KeyCode::Enter)) {
            auto action = items_[sel_].action;
            app().router.pop();
            if (action) action();
        } else if (k.code == KeyCode::Char && k.ch >= '1' && k.ch <= '9') {
            int i = k.ch - '1';
            if (i < n && items_[i].destructive) {
                sel_ = i;
            } else if (i < n) {
                auto action = items_[i].action;
                app().router.pop();
                if (action) action();
            }
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        header(c, title_);
        const int row_h = 17;
        int visible = kBodyH / row_h;
        if (sel_ < first_) first_ = sel_;
        if (sel_ >= first_ + visible) first_ = sel_ - visible + 1;
        for (int i = 0; i < visible && first_ + i < int(items_.size()); i++) {
            int idx = first_ + i;
            int y = kBodyY + i * row_h;
            list_row(c, y, row_h - 1, idx == sel_);
            char num[4];
            snprintf(num, sizeof num, "%d", idx + 1);
            text_small(c, 9, y + 5, idx < 9 ? num : " ");
            text_mono(c, 20, y, fit(items_[idx].label, 22).c_str(), items_[idx].color);
            if (!items_[idx].detail.empty()) {
                c.setFont(CV_FONT_SMALL);
                c.setTextColor(color::dim);
                c.setTextDatum(top_right);
                c.drawString(items_[idx].detail.c_str(), kW - 10, y + 5);
                c.setTextDatum(top_left);
            }
        }
        scrollbar(c, first_, visible, int(items_.size()));
        footer(c, "ENTER select  ;/. move  ` back");
    }

private:
    const char* title_;
    std::vector<MenuItem> items_;
    int sel_ = 0;
    int first_ = 0;
};

class DialogScreen : public Screen {
public:
    const char* name() const override { return confirm_ ? "confirm" : "message"; }
    const char* title() const override { return title_.c_str(); }
#ifdef CV_TEST_HOOKS
    void debug(std::string& out) const override { out = std::string("msg=") + msg_.c_str(); }
#endif
    DialogScreen(const char* title, const char* msg, uint16_t accent, bool confirm,
                 std::function<void()> yes)
        : title_(title), msg_(msg), accent_(accent), confirm_(confirm), yes_(std::move(yes)) {}

    void on_key(const Key& k) override {
        if (confirm_) {
            if (k.is('y') || k.is('Y')) {
                auto yes = yes_;
                app().router.pop();
                if (yes) yes();
            } else if (k.is('n') || k.is('N') || k.back() || k.is(KeyCode::Enter)) {
                app().router.pop();
            }
        } else if (k.is(KeyCode::Enter) || k.back() || k.is(' ')) {
            auto ok = yes_;
            app().router.pop();
            if (ok) ok();
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        header(c, "CardVault", accent_);
        dialog(c, title_.c_str(), msg_.c_str(), accent_);
        footer(c, confirm_ ? "Y yes   N / ENTER no" : "ENTER ok");
    }

private:
    SString title_, msg_;
    uint16_t accent_;
    bool confirm_;
    std::function<void()> yes_;
};

}  // namespace

ScreenPtr make_text_input(const char* title, const SString& initial, InputOpts opts,
                          TextDone done) {
    return ScreenPtr(new TextInputScreen(title, initial, opts, std::move(done)));
}

ScreenPtr make_menu(const char* title, std::vector<MenuItem> items) {
    return ScreenPtr(new MenuScreen(title, std::move(items)));
}

ScreenPtr make_confirm(const char* title, const char* msg, std::function<void()> yes,
                       uint16_t accent) {
    return ScreenPtr(new DialogScreen(title, msg, accent, true, std::move(yes)));
}

ScreenPtr make_message(const char* title, const char* msg, uint16_t accent,
                       std::function<void()> ok) {
    return ScreenPtr(new DialogScreen(title, msg, accent, false, std::move(ok)));
}

InputOpts existing_passphrase_opts(const char* hint) {
    InputOpts o;
    o.mask = true;
    o.min_len = 1;
    o.max_len = kMaxPassphraseLen;
    o.hint = hint;
    return o;
}

bool attempt_delay_active() {
    uint32_t ms = app().attempt_wait_ms();
    if (ms == 0) return false;
    char msg[40];
    snprintf(msg, sizeof msg, "Too many tries. Wait %us", unsigned((ms + 999) / 1000));
    app().toast(msg, color::warn, 2500);
    return true;
}

void ask_new_passphrase(const char* title, TextDone done) {
    InputOpts o;
    o.mask = true;
    o.strength = true;
    o.min_len = kMinPassphraseLen;
    o.max_len = kMaxPassphraseLen;
    o.hint = "Best: 6+ generated BIP39 words (Tools)";
    app().router.push(make_text_input(title, "", o, [title, done](SString& first) {
        // Same policy the vault core enforces; catch it here so the user is
        // asked again instead of failing after the confirm step.
        VaultError policy = check_new_passphrase(first);
        if (policy != VaultError::Ok) {
            app().toast(vault_error_str(policy), color::warn);
            ask_new_passphrase(title, done);
            return;
        }
        auto first_copy = std::make_shared<SecretBox>(first);
        InputOpts c;
        c.mask = true;
        c.max_len = kMaxPassphraseLen;
        c.hint = "Type it again to confirm";
        app().router.push(make_text_input("Confirm passphrase", "", c,
                                          [title, done, first_copy](SString& second) {
                                              bool match = first_copy->value == second;
                                              if (!match) {
                                                  app().toast("Passphrases did not match", color::bad);
                                                  ask_new_passphrase(title, done);
                                                  return;
                                              }
                                              done(second);
                                          }));
    }));
}

}  // namespace ui
}  // namespace cv

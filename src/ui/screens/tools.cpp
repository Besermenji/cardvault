// Seed phrase entry, password generator, 2FA overview and clock setting.
#include <M5Cardputer.h>

#include "core/entry_rules.h"
#include "core/mnemonic_entry.h"
#include "features/base32.h"
#include "features/bip39.h"
#include "features/pwgen.h"
#include "features/timeutil.h"
#include "features/totp.h"
#include "ui/app.h"

namespace cv {
namespace ui {

namespace {

class MnemonicInputScreen : public Screen {
public:
    const char* name() const override { return "mnemonic_input"; }
#ifdef CV_TEST_HOOKS
    void debug(std::string& out) const override {
        out = "index=" + std::to_string(m_.index()) + " prefix=" + m_.prefix().c_str() +
              " candidates=" + std::to_string(m_.candidate_total());
    }
#endif
    MnemonicInputScreen(int n, const SString& initial, TextDone done)
        : m_(n, initial), done_(std::move(done)) {}

    void on_key(const Key& k) override {
        invalidate();
        MnemonicEntry::Event ev = MnemonicEntry::Event::None;
        if (k.is(KeyCode::Esc)) {
            app().router.push(make_confirm("Discard words?", "The words typed so far are lost.",
                                           [] { app().router.pop(); }));
        } else if (k.code == KeyCode::Char && isalpha(uint8_t(k.ch))) {
            ev = m_.type_char(k.ch);
        } else if (k.is(KeyCode::Backspace)) {
            m_.backspace();
        } else if (k.up()) {
            m_.select_prev();
        } else if (k.down()) {
            m_.select_next();
        } else if (k.is(KeyCode::Left)) {
            m_.prev_word();
        } else if (k.is(KeyCode::Right)) {
            m_.next_word();
        } else if (k.is(KeyCode::Enter) || k.is(KeyCode::Tab) || k.is(' ')) {
            ev = m_.accept();
        }
        if (ev == MnemonicEntry::Event::Completed) finish();
    }

    void draw(Canvas& c) override {
        clear(c);
        int n = m_.count();
        int idx = m_.index();
        char title[32];
        snprintf(title, sizeof title, "Word %d of %d", idx + 1, n);
        header(c, title, color::accent);

        // Progress dots
        int dot_w = (kW - 16) / n;
        for (int i = 0; i < n; i++) {
            uint16_t col = i == idx ? color::accent : m_.word(i).empty() ? color::line : color::good;
            c.fillRoundRect(8 + i * dot_w, kBodyY + 1, dot_w - 2, 4, 2, col);
        }

        // Typed prefix (or the already accepted word)
        int y = kBodyY + 10;
        c.fillRoundRect(6, y, 110, 22, 4, color::panel);
        const SString& prefix = m_.prefix();
        const SString& shown_word = prefix.empty() ? m_.word(idx) : prefix;
        text_mono(c, 12, y + 3, shown_word.c_str(), prefix.empty() ? color::good : color::text);
        if ((millis() / 500) % 2 == 0)
            c.fillRect(12 + int(prefix.size()) * kMonoW, y + 4, 2, 14, color::accent);

        // Candidates
        int cy = kBodyY + 36;
        if (prefix.empty()) {
            text_small(c, 8, cy, "Type the first letters of the word.");
            text_small(c, 8, cy + 11, "4 letters are always enough.");
        } else if (m_.candidate_total() == 0) {
            text_small(c, 8, cy, "No BIP39 word starts like this.", color::bad);
        }
        for (int i = 0; i < m_.candidates_shown(); i++) {
            int ry = cy + i * 15;
            bool sel = i == m_.selected();
            list_row(c, ry - 1, 14, sel);
            text_mono(c, 12, ry - 2, kBip39Words[m_.candidate(i)], sel ? color::text : color::dim);
        }
        if (m_.candidate_total() > m_.candidates_shown()) {
            char more[16];
            snprintf(more, sizeof more, "+%d more", m_.candidate_total() - m_.candidates_shown());
            text_small(c, 130, cy + 3 * 15, more);
        }
        if (idx > 0) {
            char prev[24];
            snprintf(prev, sizeof prev, "#%d done", idx);
            text_small(c, 130, y + 7, prev, color::good);
        }
        footer(c, "ENTER pick ;. choose DEL back fn,/ word");
    }

    void tick(uint32_t now) override {
        bool phase = (now / 500) % 2;
        if (phase != blink_) {
            blink_ = phase;
            invalidate();
        }
    }

private:
    void finish() {
        auto box = std::make_shared<SecretBox>(m_.joined());
        TextDone done = done_;
        if (bip39_validate(box->value) == Bip39Status::Ok) {
            app().router.pop();
            done(box->value);
            app().toast("Checksum OK", color::good);
            return;
        }
        app().router.push(make_confirm(
            "Checksum invalid",
            "These words are not a valid BIP39 phrase - probably a typo. Save anyway? (N = fix)",
            [box, done] {
                app().router.pop();
                done(box->value);
            },
            color::bad));
    }

    MnemonicEntry m_;
    bool blink_ = false;
    TextDone done_;
};

// ---------------------------------------------------------------------------

class PwgenScreen : public Screen {
public:
    const char* name() const override { return "pwgen"; }
#ifdef CV_TEST_HOOKS
    void debug(std::string& out) const override {
        out = std::string("value=") + value_.c_str() + " sel=" + std::to_string(sel_);
    }
#endif
    explicit PwgenScreen(TextDone use) : use_(std::move(use)) { regenerate(); }
    ~PwgenScreen() override { wipe(value_); }

    void on_key(const Key& k) override {
        invalidate();
        if (k.back()) {
            app().router.pop();
        } else if (k.up()) {
            sel_ = (sel_ + kRows - 1) % kRows;
        } else if (k.down()) {
            sel_ = (sel_ + 1) % kRows;
        } else if (k.left() || k.right()) {
            change(k.right() ? 1 : -1);
        } else if (k.is(KeyCode::Enter)) {
            change(1);
        } else if (k.is('g') || k.is(' ')) {
            regenerate();
        } else if (k.is(KeyCode::Tab)) {
            use();
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        header(c, "Generator", color::info);
        // Output
        c.fillRoundRect(4, kBodyY, kW - 8, 36, 4, color::panel);
        const int cols = (kW - 16) / kMonoW;
        auto lines = wrap(value_.data(), value_.size(), cols, !words_);
        for (int i = 0; i < 2 && i < int(lines.size()); i++) {
            SString s(value_.data() + lines[i].start, lines[i].len);
            text_mono(c, 8, kBodyY + 2 + i * kMonoH, s.c_str());
            wipe(s);
        }
        double bits = words_ ? pwgen_words_bits(word_count_) : pwgen_password_bits(opt_);
        char b[24];
        snprintf(b, sizeof b, "%d bits", int(bits));
        text_small(c, kW - 50, kBodyY + 38, b, bits >= 80 ? color::good : color::warn);

        // Options
        const char* labels[kRows] = {"Mode", "Length", "a-z", "A-Z", "0-9", "!@#", "No 0O1lI"};
        int y = kBodyY + 38;
        static const uint8_t kCol[kRows] = {0, 0, 1, 1, 1, 2, 2};
        static const uint8_t kRow[kRows] = {0, 1, 0, 1, 2, 0, 1};
        for (int i = 0; i < kRows; i++) {
            int col = kCol[i], row = kRow[i];
            int x = 4 + col * 80;
            int ry = y + 10 + row * 13;
            if (i == sel_) c.fillRoundRect(x, ry - 1, 76, 12, 2, color::sel);
            char val[20] = "";
            switch (i) {
                case 0: snprintf(val, sizeof val, "%s", words_ ? "words" : "chars"); break;
                case 1: snprintf(val, sizeof val, "%d", words_ ? word_count_ : opt_.length); break;
                case 2: snprintf(val, sizeof val, "%s", opt_.lower ? "[x]" : "[ ]"); break;
                case 3: snprintf(val, sizeof val, "%s", opt_.upper ? "[x]" : "[ ]"); break;
                case 4: snprintf(val, sizeof val, "%s", opt_.digits ? "[x]" : "[ ]"); break;
                case 5: snprintf(val, sizeof val, "%s", opt_.symbols ? "[x]" : "[ ]"); break;
                case 6: snprintf(val, sizeof val, "%s", opt_.no_ambiguous ? "[x]" : "[ ]"); break;
            }
            bool active = i < 2 || !words_;
            text_small(c, x + 3, ry + 1, labels[i], active ? color::dim : color::line);
            text_small(c, x + 50, ry + 1, val, active ? color::text : color::line);
        }
        footer(c, use_ ? "G new  ,/ change  TAB use it  ` back"
                       : "G new  ,/ change  TAB save  ` back");
    }

private:
    static constexpr int kRows = 7;

    void change(int d) {
        switch (sel_) {
            case 0: words_ = !words_; break;
            case 1:
                if (words_)
                    word_count_ = constrain(word_count_ + d, 3, 12);
                else
                    opt_.length = constrain(opt_.length + d * (opt_.length >= 32 ? 4 : 1), 6, 64);
                break;
            case 2: opt_.lower = !opt_.lower; break;
            case 3: opt_.upper = !opt_.upper; break;
            case 4: opt_.digits = !opt_.digits; break;
            case 5: opt_.symbols = !opt_.symbols; break;
            case 6: opt_.no_ambiguous = !opt_.no_ambiguous; break;
        }
        if (!opt_.lower && !opt_.upper && !opt_.digits && !opt_.symbols) opt_.lower = true;
        regenerate();
    }

    void regenerate() {
        wipe(value_);
        value_ = words_ ? pwgen_words(word_count_, '-') : pwgen_password(opt_);
    }

    void use() {
        auto box = std::make_shared<SecretBox>(value_);
        TextDone use = use_;
        app().router.pop();
        if (use) {
            use(box->value);
        } else {
            app().router.push(make_entry_edit(0, EntryType::Password, &box->value));
        }
    }

    TextDone use_;
    PwOptions opt_;
    bool words_ = false;
    int word_count_ = 6;
    int sel_ = 1;
    SString value_;
};

// ---------------------------------------------------------------------------

class TotpListScreen : public Screen {
public:
    const char* name() const override { return "totp_list"; }
#ifdef CV_TEST_HOOKS
    void debug(std::string& out) const override {
        out = "codes=";
        uint32_t now = device::unix_time();
        bool first = true;
        for (auto& e : app().session.data().entries) {
            if (e.type != EntryType::Totp) continue;
            SBytes key;
            if (!base32_decode(e.get_or(kFieldTotpSecret), key) || key.empty()) continue;
            int digits;
            uint32_t period;
            totp_settings(e, digits, period);
            char code[10];
            totp_format(totp(key.data(), key.size(), now, period, digits), digits, code);
            if (!first) out += "|";
            first = false;
            out += std::string(e.title.c_str()) + ":" + code + ":" + std::to_string(now);
        }
    }
#endif
    void on_key(const Key& k) override {
        invalidate();
        auto ids = totp_ids();
        int n = int(ids.size());
        if (k.back()) app().router.pop();
        else if (k.is('t') || k.is('T')) ask_set_time();
        else if (k.up() && n) sel_ = (sel_ + n - 1) % n;
        else if (k.down() && n) sel_ = (sel_ + 1) % n;
        else if (k.is(KeyCode::Enter) && n) app().router.push(make_entry_view(ids[sel_]));
    }

    void tick(uint32_t now) override {
        if (now / 1000 != last_sec_) {
            last_sec_ = now / 1000;
            invalidate();
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        header(c, "2FA codes", color::good);
        auto ids = totp_ids();
        if (!device::time_valid()) {
            text_mono(c, 8, kBodyY + 10, "Clock not set", color::warn);
            text_small(c, 8, kBodyY + 32, "TOTP needs the current UTC time.");
            text_small(c, 8, kBodyY + 44, "Press T to set it.");
            footer(c, "T set clock  ` back");
            return;
        }
        if (ids.empty()) {
            text_mono(c, 8, kBodyY + 10, "No 2FA secrets", color::dim);
            text_small(c, 8, kBodyY + 32, "Add one: Menu > New secret > 2FA");
        }
        const int row_h = 26;
        int visible = kBodyH / row_h;
        if (sel_ >= int(ids.size())) sel_ = 0;
        if (sel_ < first_) first_ = sel_;
        if (sel_ >= first_ + visible) first_ = sel_ - visible + 1;
        uint32_t now = device::unix_time();
        for (int i = 0; i < visible && first_ + i < int(ids.size()); i++) {
            const Entry* e = app().session.data().find(ids[first_ + i]);
            if (!e) continue;
            int y = kBodyY + i * row_h;
            list_row(c, y, row_h - 1, first_ + i == sel_);
            text_small(c, 9, y + 3, fit(e->title, 18).c_str(), color::text);
            SBytes key;
            if (!base32_decode(e->get_or(kFieldTotpSecret), key) || key.empty()) {
                text_small(c, 9, y + 14, "invalid secret", color::bad);
                continue;
            }
            int digits;
            uint32_t period;
            totp_settings(*e, digits, period);
            char code[10];
            totp_format(totp(key.data(), key.size(), now, period, digits), digits, code);
            uint32_t left = period - now % period;
            uint16_t col = left <= 5 ? color::bad : color::good;
            c.setFont(&fonts::FreeMonoBold12pt7b);
            c.setTextColor(col);
            c.setTextDatum(middle_right);
            c.drawString(code, kW - 12, y + 12);
            c.setTextDatum(top_left);
            progress(c, 9, y + 17, 80, 3, float(left) / period, col);
        }
        scrollbar(c, first_, visible, int(ids.size()));
        footer(c, "ENTER details  T set clock  ` back");
    }

private:
    std::vector<uint32_t> totp_ids() {
        std::vector<uint32_t> ids;
        for (auto& e : app().session.data().entries)
            if (e.type == EntryType::Totp) ids.push_back(e.id);
        return ids;
    }

    int sel_ = 0;
    int first_ = 0;
    uint32_t last_sec_ = 0;
};

}  // namespace

ScreenPtr make_mnemonic_input(int words, const SString& initial, TextDone done) {
    return ScreenPtr(new MnemonicInputScreen(words, initial, std::move(done)));
}
ScreenPtr make_pwgen(TextDone use) { return ScreenPtr(new PwgenScreen(std::move(use))); }
ScreenPtr make_totp_list() { return ScreenPtr(new TotpListScreen()); }

void start_mnemonic_flow(const SString& current, TextDone done) {
    std::vector<MenuItem> items;
    auto box = std::make_shared<SecretBox>(current);
    int existing = int(bip39_split(current).size());
    bool has_words = existing > 0;
    if (bip39_valid_word_count(existing)) {
        items.push_back({"Edit current words", [box, done, existing] {
                             app().router.push(make_mnemonic_input(existing, box->value, done));
                         }});
    }
    for (int n : {12, 24, 18}) {
        char label[24];
        snprintf(label, sizeof label, "Type %d words", n);
        items.push_back({label, [n, done] { app().router.push(make_mnemonic_input(n, "", done)); }});
    }
    for (int n : {12, 24}) {
        char label[24];
        snprintf(label, sizeof label, "Generate %d words", n);
        auto generate = [n, done] {
            app().busy("Generating...");
            SString words;
            bip39_generate(n, words);
            done(words);
            wipe(words);
            app().router.push(make_message(
                "Write it down!",
                "A new seed was generated with the hardware RNG. Save the "
                "entry, then copy the words to paper as a backup.",
                color::accent));
        };
        if (!has_words) {
            items.push_back({label, generate, "RNG", color::accent});
            continue;
        }
        // Replacing existing words: confirm, and a digit hotkey only selects it.
        items.push_back({label,
                         [generate] {
                             app().router.push(make_confirm(
                                 "Replace seed words?",
                                 "The new words replace the current ones when you save.", generate,
                                 color::bad));
                         },
                         "RNG", color::accent, true});
    }
    items.push_back({"Free text (non-BIP39)", [box, done] {
                         InputOpts o;
                         o.multiline = true;
                         o.hint = "Any words/format. TAB when done.";
                         app().router.push(make_text_input("Seed words", box->value, o, done));
                     }});
    app().router.push(make_menu("Seed phrase", std::move(items)));
}

void ask_set_time(std::function<void()> done) {
    // Pre-fill the real time when the clock is set; never a made-up date.
    char now[20] = "";
    if (device::time_valid()) {
        format_utc(device::unix_time(), now);
        now[16] = '\0';  // drop seconds
    }
    InputOpts o;
    o.hint = "UTC time: YYYY-MM-DD HH:MM[:SS]";
    o.max_len = 19;
    app().router.push(make_text_input("Set clock (UTC)", now, o, [done](SString& v) {
        uint32_t t;
        if (!parse_utc(v.c_str(), t)) {
            app().toast("Invalid date/time", color::bad);
            ask_set_time(done);
            return;
        }
        if (t < kClockFloor) {
            app().toast("Date before 2025-01-01", color::bad);
            ask_set_time(done);
            return;
        }
        device::set_unix_time(t);
        app().toast("Clock set");
        if (done) done();
    }));
}

}  // namespace ui
}  // namespace cv

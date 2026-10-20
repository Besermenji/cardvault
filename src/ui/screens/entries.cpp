// Vault browser: home list, entry view, editor and secret viewers.
#include <M5Cardputer.h>

#include <algorithm>

#include "core/entry_rules.h"
#include "features/base32.h"
#include "features/bip39.h"
#include "features/totp.h"
#include "ui/app.h"

namespace cv {
namespace ui {

namespace {

bool icontains(const SString& hay, const SString& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return tolower(a) == tolower(b); });
    return it != hay.end();
}

// Computes the current TOTP code for an entry. Returns false on bad secret.
bool totp_now(const Entry& e, char* code, int& digits, uint32_t& period) {
    SBytes key;
    if (!base32_decode(e.get_or(kFieldTotpSecret), key) || key.empty()) return false;
    digits = atoi(e.get_or(kFieldTotpDigits, "6").c_str());
    if (digits != 6 && digits != 8) digits = 6;
    period = uint32_t(atoi(e.get_or(kFieldTotpPeriod, "30").c_str()));
    if (period < 10 || period > 300) period = 30;
    uint32_t v = totp(key.data(), key.size(), device::unix_time(), period, digits);
    totp_format(v, digits, code);
    return true;
}

// Splits a TOTP code in two groups for readability ("123 456").
void draw_code(Canvas& c, int cx, int cy, const char* code, int digits, uint16_t col) {
    char spaced[12];
    int half = digits / 2;
    snprintf(spaced, sizeof spaced, "%.*s %s", half, code, code + half);
    c.setFont(CV_FONT_BIG);
    c.setTextColor(col);
    c.setTextDatum(middle_center);
    c.drawString(spaced, cx, cy);
    c.setTextDatum(top_left);
}

// ---------------------------------------------------------------------------

class HomeScreen : public Screen {
public:
    const char* name() const override { return "home"; }
    HomeScreen() { refresh(); }

    void on_resume() override {
        refresh();
        invalidate();
    }

    void on_key(const Key& k) override {
        invalidate();
        int n = int(ids_.size());
        if (k.is(KeyCode::Tab)) {
            open_main_menu();
        } else if (k.is(KeyCode::Enter)) {
            if (n) app().router.push(make_entry_view(ids_[sel_]));
            else open_new_entry_menu();
        } else if (k.is(KeyCode::Backspace)) {
            if (!query_.empty()) {
                query_.pop_back();
                refresh();
            }
        } else if (k.back()) {
            if (!query_.empty()) {
                wipe(query_);
                refresh();
            } else {
                app().lock();
            }
        } else if (k.up()) {
            if (n) sel_ = (sel_ + n - 1) % n;
        } else if (k.down()) {
            if (n) sel_ = (sel_ + 1) % n;
        } else if (k.code == KeyCode::Char && (isalnum(uint8_t(k.ch)) || k.ch == ' ' ||
                                                k.ch == '-' || k.ch == '_')) {
            query_ += k.ch;
            sel_ = 0;
            refresh();
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        char title[32];
        snprintf(title, sizeof title, "Vault  (%u)", unsigned(app().session.data().entries.size()));
        header(c, title);
        int y = kBodyY;
        if (!query_.empty()) {
            c.fillRoundRect(4, y, kW - 8, 13, 3, color::panel);
            SString q = SString("search: ") + query_;
            text_small(c, 8, y + 3, q.c_str(), color::accent);
            y += 15;
        }
        const int row_h = 18;
        int visible = (kBodyY + kBodyH - y) / row_h;
        if (ids_.empty()) {
            text_mono(c, 10, y + 16, query_.empty() ? "No secrets yet." : "No matches.",
                      color::dim);
            if (query_.empty()) text_small(c, 10, y + 38, "Press ENTER to add your first one.");
        }
        if (sel_ < first_) first_ = sel_;
        if (sel_ >= first_ + visible) first_ = sel_ - visible + 1;
        auto& data = app().session.data();
        for (int i = 0; i < visible && first_ + i < int(ids_.size()); i++) {
            const Entry* e = data.find(ids_[first_ + i]);
            if (!e) continue;
            int ry = y + i * row_h;
            list_row(c, ry, row_h - 1, first_ + i == sel_);
            type_badge(c, 8, ry + 1, e->type);
            text_mono(c, 28, ry, fit(e->title, 24).c_str());
        }
        scrollbar(c, first_, visible, int(ids_.size()));
        footer(c, "ENTER open TAB menu abc=search ` lock");
    }

private:
    void refresh() {
        auto& entries = app().session.data().entries;
        std::vector<const Entry*> list;
        for (auto& e : entries)
            if (icontains(e.title, query_)) list.push_back(&e);
        std::sort(list.begin(), list.end(), [](const Entry* a, const Entry* b) {
            return std::lexicographical_compare(
                a->title.begin(), a->title.end(), b->title.begin(), b->title.end(),
                [](char x, char y) { return tolower(x) < tolower(y); });
        });
        ids_.clear();
        for (auto* e : list) ids_.push_back(e->id);
        if (sel_ >= int(ids_.size())) sel_ = ids_.empty() ? 0 : int(ids_.size()) - 1;
    }

    void open_main_menu() {
        std::vector<MenuItem> items;
        items.push_back({"New secret", [] { open_new_entry_menu(); }});
        items.push_back({"2FA codes", [] { app().router.push(make_totp_list()); }});
        items.push_back({"Password generator", [] { app().router.push(make_pwgen(nullptr)); }});
        items.push_back({"Settings", [] { app().router.push(make_settings()); }});
        items.push_back({"Lock now", [] { app().lock(); }, "G0", color::accent});
        app().router.push(make_menu("Menu", std::move(items)));
    }

    SString query_;
    std::vector<uint32_t> ids_;
    int sel_ = 0;
    int first_ = 0;
};

// ---------------------------------------------------------------------------

class EntryViewScreen : public Screen {
public:
    const char* name() const override { return "entry_view"; }
    explicit EntryViewScreen(uint32_t id) : id_(id) {}

    void on_key(const Key& k) override {
        invalidate();
        Entry* e = entry();
        if (!e) {
            app().router.pop();
            return;
        }
        int nf;
        const uint8_t* fields = entry_type_fields(e->type, nf);
        if (k.back()) {
            app().router.pop();
        } else if (k.up()) {
            sel_ = (sel_ + nf - 1) % nf;
        } else if (k.down()) {
            sel_ = (sel_ + 1) % nf;
        } else if (k.is('r') || k.is('R')) {
            reveal_all_ = !reveal_all_;
        } else if (k.is('e') || k.is('E')) {
            app().router.push(make_entry_edit(id_, e->type));
        } else if (k.is('t') && e->type == EntryType::Totp) {
            ask_set_time();
        } else if (k.is(KeyCode::Enter)) {
            uint8_t f = fields[sel_];
            if (f == kFieldWords)
                app().router.push(make_seed_view(id_));
            else if (field_is_multiline(f) || (e->get(f) && e->get(f)->size() > 26))
                app().router.push(make_text_view(id_, f));
            else
                reveal_all_ = !reveal_all_;
        } else if (k.is(KeyCode::Tab)) {
            open_actions();
        }
    }

    void tick(uint32_t now) override {
        // Hold-to-reveal and live TOTP refresh.
        bool held = key_held(' ');
        if (held != held_) {
            held_ = held;
            invalidate();
        }
        uint32_t sec = now / 1000;
        if (sec != last_sec_) {
            last_sec_ = sec;
            Entry* e = entry();
            if (e && e->type == EntryType::Totp) invalidate();
        }
    }

    void draw(Canvas& c) override {
        Entry* e = entry();
        clear(c);
        if (!e) return;
        header(c, fit(e->title, 20).c_str(), type_color(e->type));
        int y = kBodyY;

        if (e->type == EntryType::Totp) {
            char code[10];
            int digits;
            uint32_t period;
            if (!device::time_valid()) {
                text_mono(c, 8, y + 8, "Clock not set", color::warn);
                text_small(c, 8, y + 28, "Press T to set the UTC time.");
            } else if (totp_now(*e, code, digits, period)) {
                uint32_t left = period - device::unix_time() % period;
                uint16_t col = left <= 5 ? color::bad : color::good;
                draw_code(c, kW / 2, y + 20, code, digits, col);
                progress(c, 20, y + 42, kW - 40, 4, float(left) / period, col);
            } else {
                text_mono(c, 8, y + 8, "Invalid secret", color::bad);
            }
            y += 50;
        }

        int nf;
        const uint8_t* fields = entry_type_fields(e->type, nf);
        const int row_h = 24;
        int visible = (kBodyY + kBodyH - y) / row_h;
        if (visible < 1) visible = 1;
        if (sel_ < first_) first_ = sel_;
        if (sel_ >= first_ + visible) first_ = sel_ - visible + 1;
        for (int i = 0; i < visible && first_ + i < nf; i++) {
            int idx = first_ + i;
            uint8_t f = fields[idx];
            int ry = y + i * row_h;
            list_row(c, ry, row_h - 1, idx == sel_);
            text_small(c, 9, ry + 2, field_label(f));
            const SString* v = e->get(f);
            SString value = v ? *v : SString();
            bool hidden = field_is_secret(f) && !reveal_all_ && !(held_ && idx == sel_);
            if (value.empty()) {
                text_mono(c, 9, ry + 8, "-", color::line);
            } else if (hidden) {
                mask(c, 9, ry + 8, value.size(), 26);
            } else {
                SString first_line = value.substr(0, value.find('\n'));
                text_mono(c, 9, ry + 8, fit(first_line, 27).c_str());
            }
            wipe(value);
        }
        scrollbar(c, first_, visible, nf);
        footer(c, "SPACE peek  R reveal  ENTER  TAB more");
    }

private:
    Entry* entry() { return app().session.data().find(id_); }

    void open_actions() {
        uint32_t id = id_;
        Entry* e = entry();
        if (!e) return;
        std::vector<MenuItem> items;
        items.push_back({"Edit", [id] {
                             Entry* x = app().session.data().find(id);
                             if (x) app().router.push(make_entry_edit(id, x->type));
                         }});
        if (e->type == EntryType::Mnemonic)
            items.push_back({"Show words", [id] { app().router.push(make_seed_view(id)); }});
        if (e->type == EntryType::Totp) items.push_back({"Set clock", [] { ask_set_time(); }});
        items.push_back({"Delete", [id] {
                             app().router.push(make_confirm(
                                 "Delete secret?", "This cannot be undone (unless you have a backup).",
                                 [id] {
                                     app().session.data().remove(id);
                                     if (app().save()) app().toast("Deleted", color::warn);
                                     app().router.pop();  // leave the (now empty) entry view
                                 },
                                 color::bad));
                         },
                         "", color::bad});
        app().router.push(make_menu("Secret", std::move(items)));
    }

    uint32_t id_;
    int sel_ = 0;
    int first_ = 0;
    bool reveal_all_ = false;
    bool held_ = false;
    uint32_t last_sec_ = 0;
};

// ---------------------------------------------------------------------------

class EntryEditScreen : public Screen {
public:
    const char* name() const override { return "entry_edit"; }
    EntryEditScreen(uint32_t id, EntryType type, const SString* prefill) : id_(id) {
        if (const Entry* src = app().session.data().find(id)) {
            draft_ = *src;
        } else {
            draft_.type = type;
            if (type == EntryType::Totp) {
                draft_.set(kFieldTotpDigits, "6");
                draft_.set(kFieldTotpPeriod, "30");
            }
            if (prefill && type == EntryType::Password) draft_.set(kFieldPassword, *prefill);
        }
    }
    ~EntryEditScreen() override { draft_.clear(); }

    void on_key(const Key& k) override {
        invalidate();
        int nf;
        const uint8_t* fields = entry_type_fields(draft_.type, nf);
        int rows = nf + 2;  // title + fields + save
        if (k.back()) {
            if (!dirty_) {
                app().router.pop();
                return;
            }
            app().router.push(make_confirm("Discard changes?", "Your edits will be lost.",
                                           [] { app().router.pop(); }));
        } else if (k.up()) {
            sel_ = (sel_ + rows - 1) % rows;
        } else if (k.down()) {
            sel_ = (sel_ + 1) % rows;
        } else if (k.is(KeyCode::Tab)) {
            save();
        } else if (k.is('g') && sel_ > 0 && sel_ <= nf && fields[sel_ - 1] == kFieldPassword) {
            generate();
        } else if (k.left() || k.right()) {
            if (sel_ > 0 && sel_ <= nf) cycle(fields[sel_ - 1], k.right());
        } else if (k.is(KeyCode::Enter)) {
            if (sel_ == 0)
                edit_title();
            else if (sel_ <= nf)
                edit_field(fields[sel_ - 1]);
            else
                save();
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        char title[32];
        snprintf(title, sizeof title, "%s %s", id_ ? "Edit" : "New", entry_type_name(draft_.type));
        header(c, title, type_color(draft_.type));
        int nf;
        const uint8_t* fields = entry_type_fields(draft_.type, nf);
        int rows = nf + 2;
        const int row_h = 21;
        int visible = kBodyH / row_h;
        if (sel_ < first_) first_ = sel_;
        if (sel_ >= first_ + visible) first_ = sel_ - visible + 1;
        for (int i = 0; i < visible && first_ + i < rows; i++) {
            int idx = first_ + i;
            int y = kBodyY + i * row_h;
            list_row(c, y, row_h - 1, idx == sel_);
            if (idx == rows - 1) {
                text_mono(c, 10, y + 2, "[ Save ]", color::accent);
                continue;
            }
            uint8_t f = idx == 0 ? 0 : fields[idx - 1];
            text_small(c, 9, y + 1, idx == 0 ? "Title" : field_label(f));
            SString v = idx == 0 ? draft_.title : draft_.get_or(f);
            if (v.empty()) {
                text_small(c, 9, y + 11, f == kFieldPassword ? "(empty - ENTER type, G generate)"
                                                             : "(empty - ENTER to edit)",
                           color::line);
            } else if (field_is_secret(f)) {
                mask(c, 9, y + 9, v.size(), 26);
            } else {
                text_small(c, 9, y + 11, fit(v, 37).c_str(), color::text);
            }
            wipe(v);
        }
        scrollbar(c, first_, visible, rows);
        footer(c, "ENTER edit  TAB save  ` cancel");
    }

private:
    void edit_title() {
        InputOpts o;
        o.max_len = 60;
        app().router.push(make_text_input("Title", draft_.title, o, [this](SString& v) {
            draft_.title = v;
            dirty_ = true;
        }));
    }

    void edit_field(uint8_t f) {
        if (f == kFieldTotpDigits || f == kFieldTotpPeriod) {
            cycle(f, true);
            return;
        }
        if (f == kFieldWords) {
            start_mnemonic_flow(draft_.get_or(f), [this](SString& v) {
                draft_.set(kFieldWords, v);
                dirty_ = true;
            });
            return;
        }
        InputOpts o;
        o.multiline = field_is_multiline(f);
        o.mask = f == kFieldPassword || f == kFieldBip39Pass;
        if (f == kFieldTotpSecret) o.hint = "Base32 secret from the QR/setup key";
        app().router.push(make_text_input(field_label(f), draft_.get_or(f), o, [this, f](SString& v) {
            draft_.set(f, v);
            dirty_ = true;
        }));
    }

    void cycle(uint8_t f, bool fwd) {
        if (f == kFieldTotpDigits) {
            draft_.set(f, draft_.get_or(f, "6") == "6" ? "8" : "6");
            dirty_ = true;
        } else if (f == kFieldTotpPeriod) {
            draft_.set(f, draft_.get_or(f, "30") == "30" ? "60" : "30");
            dirty_ = true;
        }
        (void)fwd;
    }

    void generate() {
        app().router.push(make_pwgen([this](SString& v) {
            draft_.set(kFieldPassword, v);
            dirty_ = true;
        }));
    }

    void save() {
        if (const char* err = finalize_entry(draft_)) {
            app().toast(err, color::bad);
            return;
        }
        uint32_t now = device::unix_time();
        auto& data = app().session.data();
        draft_.updated = now;
        uint32_t id = id_;
        Entry previous;
        if (Entry* existing = data.find(id_)) {
            previous = *existing;
            existing->clear();
            *existing = draft_;
        } else {
            draft_.created = now;
            id = data.add(draft_);
        }
        if (!app().save()) {
            // Keep memory consistent with flash: undo the change.
            if (Entry* e = data.find(id)) {
                if (id_) {
                    e->clear();
                    *e = previous;
                } else {
                    data.remove(id);
                }
            }
            previous.clear();
            return;
        }
        previous.clear();
        dirty_ = false;
        app().toast("Saved");
        app().router.pop();
        if (!id_) app().router.push(make_entry_view(id));
    }

    uint32_t id_;
    Entry draft_;
    int sel_ = 0;
    int first_ = 0;
    bool dirty_ = false;
};

// ---------------------------------------------------------------------------

class SeedViewScreen : public Screen {
public:
    const char* name() const override { return "seed_view"; }
    explicit SeedViewScreen(uint32_t id) {
        if (const Entry* e = app().session.data().find(id)) {
            words_ = bip39_split(e->get_or(kFieldWords));
            status_ = bip39_validate(e->get_or(kFieldWords));
            has_pass_ = !e->get_or(kFieldBip39Pass).empty();
        }
    }
    ~SeedViewScreen() override {
        for (auto& w : words_) wipe(w);
    }

    void on_key(const Key& k) override {
        invalidate();
        int pages = page_count();
        if (k.back()) app().router.pop();
        else if (k.left() || k.up()) page_ = (page_ + pages - 1) % pages;
        else if (k.right() || k.down() || k.is(KeyCode::Enter)) page_ = (page_ + 1) % pages;
        else if (k.is('r') || k.is('R')) reveal_ = !reveal_;
    }

    void tick(uint32_t) override {
        bool held = key_held(' ');
        if (held != held_) {
            held_ = held;
            invalidate();
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        char title[32];
        snprintf(title, sizeof title, "Seed  %d/%d", page_ + 1, page_count());
        header(c, title, color::accent);
        bool show = reveal_ || held_;
        int start = page_ * kPerPage;
        for (int i = 0; i < kPerPage && start + i < int(words_.size()); i++) {
            int col = i / 3, row = i % 3;
            int x = 6 + col * 118, y = kBodyY + 4 + row * 28;
            c.fillRoundRect(x, y, 112, 24, 4, color::panel);
            char num[4];
            snprintf(num, sizeof num, "%d", start + i + 1);
            c.setFont(CV_FONT_SMALL);
            c.setTextColor(color::accent);
            c.setTextDatum(middle_right);
            c.drawString(num, x + 18, y + 12);
            c.setTextDatum(top_left);
            if (show)
                text_mono(c, x + 24, y + 4, words_[start + i].c_str());
            else
                mask(c, x + 24, y + 4, 6, 8);
        }
        const char* st = status_ == Bip39Status::Ok ? "BIP39 OK" : "not BIP39";
        char foot[64];
        snprintf(foot, sizeof foot, "SPACE/R reveal ,/ page %s%s", st,
                 has_pass_ ? " +pass" : "");
        footer(c, foot);
    }

private:
    static constexpr int kPerPage = 6;
    int page_count() const {
        int n = int(words_.size());
        return n == 0 ? 1 : (n + kPerPage - 1) / kPerPage;
    }

    SVec<SString> words_;
    Bip39Status status_ = Bip39Status::BadWordCount;
    bool has_pass_ = false;
    int page_ = 0;
    bool reveal_ = false;
    bool held_ = false;
};

// ---------------------------------------------------------------------------

class TextViewScreen : public Screen {
public:
    const char* name() const override { return "text_view"; }
    TextViewScreen(uint32_t id, uint8_t field) : field_(field) {
        if (const Entry* e = app().session.data().find(id)) {
            text_ = e->get_or(field);
            title_ = e->title;
        }
    }
    ~TextViewScreen() override {
        wipe(text_);
        wipe(title_);
    }

    void on_key(const Key& k) override {
        invalidate();
        if (k.back() || k.is(KeyCode::Enter)) app().router.pop();
        else if (k.up()) scroll_ = scroll_ > 0 ? scroll_ - 1 : 0;
        else if (k.down()) scroll_++;
        else if (k.is('r') || k.is('R')) reveal_ = !reveal_;
    }

    void tick(uint32_t) override {
        bool held = key_held(' ');
        if (held != held_) {
            held_ = held;
            invalidate();
        }
    }

    void draw(Canvas& c) override {
        clear(c);
        header(c, fit(title_, 20).c_str(), color::purple);
        const int cols = (kW - 14) / kMonoW;
        const int rows = kBodyH / kMonoH;
        auto lines = wrap(text_.data(), text_.size(), cols);
        int max_scroll = int(lines.size()) > rows ? int(lines.size()) - rows : 0;
        if (scroll_ > max_scroll) scroll_ = max_scroll;
        bool show = reveal_ || held_ || !field_is_secret(field_);
        for (int r = 0; r < rows && scroll_ + r < int(lines.size()); r++) {
            const Line& l = lines[scroll_ + r];
            int y = kBodyY + r * kMonoH;
            if (show) {
                SString s(text_.data() + l.start, l.len);
                text_mono(c, 5, y, s.c_str());
                wipe(s);
            } else {
                mask(c, 5, y, l.len, cols);
            }
        }
        scrollbar(c, scroll_, rows, int(lines.size()));
        footer(c, "SPACE peek R reveal ;. scroll ` back");
    }

private:
    uint8_t field_;
    SString text_, title_;
    int scroll_ = 0;
    bool reveal_ = false;
    bool held_ = false;
};

}  // namespace

ScreenPtr make_home() { return ScreenPtr(new HomeScreen()); }
ScreenPtr make_entry_view(uint32_t id) { return ScreenPtr(new EntryViewScreen(id)); }
ScreenPtr make_entry_edit(uint32_t id, EntryType type, const SString* prefill) {
    return ScreenPtr(new EntryEditScreen(id, type, prefill));
}
ScreenPtr make_seed_view(uint32_t id) { return ScreenPtr(new SeedViewScreen(id)); }
ScreenPtr make_text_view(uint32_t id, uint8_t field) {
    return ScreenPtr(new TextViewScreen(id, field));
}

void open_new_entry_menu() {
    std::vector<MenuItem> items;
    for (EntryType t : {EntryType::Mnemonic, EntryType::Password, EntryType::Totp, EntryType::Note}) {
        items.push_back({entry_type_name(t), [t] { app().router.push(make_entry_edit(0, t)); },
                         "", type_color(t)});
    }
    app().router.push(make_menu("New secret", std::move(items)));
}

}  // namespace ui
}  // namespace cv

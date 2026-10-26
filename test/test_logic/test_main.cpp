// Seed-word entry, text layout/cursor and entry save rules.
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "core/entry_rules.h"
#include "core/messages.h"
#include "core/mnemonic_entry.h"
#include "ui/textwrap.h"

using namespace cv;
using namespace cv::ui;

void setUp() {}
void tearDown() {}

static MnemonicEntry::Event type(MnemonicEntry& m, const char* s) {
    MnemonicEntry::Event ev = MnemonicEntry::Event::None;
    for (const char* p = s; *p; p++) ev = m.type_char(*p);
    return ev;
}

// ---- Mnemonic entry ---------------------------------------------------------

void test_mnemonic_autoaccepts_unique_four_letter_prefix() {
    MnemonicEntry m(12, "");
    TEST_ASSERT_EQUAL(int(MnemonicEntry::Event::None), int(type(m, "aba")));
    TEST_ASSERT_EQUAL(0, m.index());
    TEST_ASSERT_EQUAL(int(MnemonicEntry::Event::Accepted), int(m.type_char('n')));
    TEST_ASSERT_EQUAL_STRING("abandon", m.word(0).c_str());
    TEST_ASSERT_EQUAL(1, m.index());
    TEST_ASSERT_TRUE(m.prefix().empty());
}

void test_mnemonic_full_phrase_completes_and_validates() {
    MnemonicEntry m(12, "");
    for (int i = 0; i < 11; i++) type(m, "aban");
    TEST_ASSERT_EQUAL(int(MnemonicEntry::Event::Completed), int(type(m, "abou")));
    TEST_ASSERT_TRUE(m.complete());
    SString j = m.joined();
    TEST_ASSERT_EQUAL(int(Bip39Status::Ok), int(bip39_validate(j)));
}

void test_mnemonic_short_words_need_enter_and_selection() {
    MnemonicEntry m(12, "");
    type(m, "act");  // "act", "action", "actor", "actress", "actual" all match
    TEST_ASSERT_TRUE(m.candidate_total() > 1);
    TEST_ASSERT_EQUAL_STRING("act", kBip39Words[m.candidate(0)]);
    TEST_ASSERT_EQUAL(int(MnemonicEntry::Event::Accepted), int(m.accept()));
    TEST_ASSERT_EQUAL_STRING("act", m.word(0).c_str());
    type(m, "act");
    m.select_next();
    m.accept();
    TEST_ASSERT_EQUAL_STRING("action", m.word(1).c_str());
    // Selection wraps around the shown candidates.
    type(m, "act");
    for (int i = 0; i < MnemonicEntry::kMaxCandidates; i++) m.select_next();
    TEST_ASSERT_EQUAL(0, m.selected());
    m.select_prev();
    TEST_ASSERT_EQUAL(m.candidates_shown() - 1, m.selected());
}

void test_mnemonic_uppercase_and_junk() {
    MnemonicEntry m(12, "");
    type(m, "ZO");
    TEST_ASSERT_EQUAL_STRING("zo", m.prefix().c_str());
    m.type_char('1');
    m.type_char(' ');
    TEST_ASSERT_EQUAL_STRING("zo", m.prefix().c_str());
    type(m, "qqqqqqqqqqqq");  // capped at 8 letters, no candidates
    TEST_ASSERT_EQUAL(0, m.candidate_total());
    TEST_ASSERT_EQUAL(int(MnemonicEntry::Event::None), int(m.accept()));
    TEST_ASSERT_EQUAL(0, m.index());
}

void test_mnemonic_backspace_into_previous_word() {
    MnemonicEntry m(12, "");
    type(m, "aban");
    type(m, "abil");
    TEST_ASSERT_EQUAL(2, m.index());
    m.backspace();  // empty prefix -> reopen "ability" minus its last letter
    TEST_ASSERT_EQUAL(1, m.index());
    TEST_ASSERT_EQUAL_STRING("abilit", m.prefix().c_str());
    TEST_ASSERT_TRUE(m.word(1).empty());
    m.type_char('y');
    TEST_ASSERT_EQUAL_STRING("ability", m.word(1).c_str());
    TEST_ASSERT_EQUAL(2, m.index());
    // Backspace at the very first, empty word does nothing.
    MnemonicEntry fresh(12, "");
    fresh.backspace();
    TEST_ASSERT_EQUAL(0, fresh.index());
}

void test_mnemonic_word_navigation_and_prefill() {
    SString phrase = "abandon abandon abandon abandon abandon abandon abandon abandon "
                     "abandon abandon abandon about";
    MnemonicEntry m(12, phrase);
    TEST_ASSERT_TRUE(m.complete());
    TEST_ASSERT_EQUAL(11, m.index());
    m.prev_word();
    TEST_ASSERT_EQUAL(10, m.index());
    m.next_word();
    TEST_ASSERT_EQUAL(11, m.index());
    m.next_word();  // already last
    TEST_ASSERT_EQUAL(11, m.index());
    // Accepting a filled word without typing advances / completes.
    TEST_ASSERT_EQUAL(int(MnemonicEntry::Event::Completed), int(m.accept()));
    // Mismatched prefill length is ignored.
    MnemonicEntry other(24, phrase);
    TEST_ASSERT_FALSE(other.complete());
    TEST_ASSERT_EQUAL(0, other.index());
}

void test_mnemonic_cannot_skip_an_empty_word() {
    MnemonicEntry m(12, "");
    for (int i = 0; i < 3; i++) type(m, "aban");
    type(m, "ab");  // partial prefix on word 4
    m.prev_word();  // leaving with an incomplete prefix keeps word 4 empty
    TEST_ASSERT_EQUAL(2, m.index());
    TEST_ASSERT_TRUE(m.word(3).empty());
    m.next_word();
    TEST_ASSERT_EQUAL(3, m.index());
    m.next_word();  // blocked: word 4 is still empty
    TEST_ASSERT_EQUAL(3, m.index());
    TEST_ASSERT_EQUAL(int(MnemonicEntry::Event::None), int(m.accept()));
    TEST_ASSERT_FALSE(m.complete());
}

void test_mnemonic_prev_word_keeps_a_complete_prefix() {
    MnemonicEntry m(12, "");
    type(m, "aban");
    type(m, "act");  // "act" is a full word but ambiguous, not yet accepted
    m.prev_word();
    TEST_ASSERT_EQUAL(0, m.index());
    TEST_ASSERT_EQUAL_STRING("act", m.word(1).c_str());
}

void test_mnemonic_editing_a_prefilled_word() {
    SString phrase = "abandon abandon abandon abandon abandon abandon abandon abandon "
                     "abandon abandon abandon about";
    MnemonicEntry m(12, phrase);
    for (int i = 0; i < 6; i++) m.prev_word();
    TEST_ASSERT_EQUAL(5, m.index());
    m.backspace();  // reopens "abandon" as "abando"
    TEST_ASSERT_EQUAL_STRING("abando", m.prefix().c_str());
    TEST_ASSERT_FALSE(m.complete());
    for (int i = 0; i < 6; i++) m.backspace();
    TEST_ASSERT_TRUE(m.prefix().empty());
    type(m, "zoo");
    TEST_ASSERT_EQUAL(int(MnemonicEntry::Event::Accepted), int(m.accept()));
    TEST_ASSERT_EQUAL_STRING("zoo", m.word(5).c_str());
    TEST_ASSERT_TRUE(m.complete());
    SString j = m.joined();
    TEST_ASSERT_EQUAL(int(Bip39Status::BadChecksum), int(bip39_validate(j)));
}

// ---- Text layout ------------------------------------------------------------

static std::string line_str(const char* t, const Line& l) { return std::string(t + l.start, l.len); }

void test_wrap_soft_and_hard() {
    const char* t = "hello world foo";
    auto soft = wrap(t, strlen(t), 11);
    TEST_ASSERT_EQUAL(2, soft.size());
    TEST_ASSERT_EQUAL_STRING("hello world", line_str(t, soft[0]).c_str());
    TEST_ASSERT_EQUAL_STRING("foo", line_str(t, soft[1]).c_str());
    // A full line followed by a space must not lose a word to the next line.
    auto exact = wrap("abc def", 7, 3);
    TEST_ASSERT_EQUAL(2, exact.size());
    TEST_ASSERT_EQUAL_STRING("abc", line_str("abc def", exact[0]).c_str());
    TEST_ASSERT_EQUAL_STRING("def", line_str("abc def", exact[1]).c_str());
    auto soft2 = wrap(t, strlen(t), 8);
    TEST_ASSERT_EQUAL_STRING("hello", line_str(t, soft2[0]).c_str());
    auto hard = wrap(t, strlen(t), 8, true);
    TEST_ASSERT_EQUAL_STRING("hello wo", line_str(t, hard[0]).c_str());
    TEST_ASSERT_EQUAL_STRING("rld foo", line_str(t, hard[1]).c_str());
}

void test_wrap_newlines_and_empty() {
    auto e = wrap("", 0, 10);
    TEST_ASSERT_EQUAL(1, e.size());
    TEST_ASSERT_EQUAL(0, e[0].len);
    const char* t = "ab\n\ncd\n";
    auto l = wrap(t, strlen(t), 10);
    TEST_ASSERT_EQUAL(4, l.size());
    TEST_ASSERT_EQUAL_STRING("ab", line_str(t, l[0]).c_str());
    TEST_ASSERT_EQUAL(0, l[1].len);
    TEST_ASSERT_EQUAL_STRING("cd", line_str(t, l[2]).c_str());
    TEST_ASSERT_EQUAL(0, l[3].len);  // cursor line after trailing newline
    // A word longer than the width is split.
    const char* longw = "abcdefghijkl";
    auto lw = wrap(longw, strlen(longw), 5);
    TEST_ASSERT_EQUAL(3, lw.size());
}

void test_cursor_locate_and_move() {
    const char* t = "ab\ncdef";
    auto l = wrap(t, strlen(t), 10, true);
    int line;
    size_t col;
    cursor_locate(l, 0, 10, line, col);
    TEST_ASSERT_EQUAL(0, line);
    TEST_ASSERT_EQUAL(0, col);
    cursor_locate(l, 2, 10, line, col);  // end of "ab", before '\n'
    TEST_ASSERT_EQUAL(0, line);
    TEST_ASSERT_EQUAL(2, col);
    cursor_locate(l, 3, 10, line, col);  // start of "cdef"
    TEST_ASSERT_EQUAL(1, line);
    TEST_ASSERT_EQUAL(0, col);
    cursor_locate(l, 7, 10, line, col);  // end of text
    TEST_ASSERT_EQUAL(1, line);
    TEST_ASSERT_EQUAL(4, col);
    // Down from column 2 of line 0 -> column 2 of "cdef".
    TEST_ASSERT_EQUAL(5, cursor_move_line(l, 2, 10, 1));
    // Up from end of "cdef" (col 4) -> clamps to end of "ab".
    TEST_ASSERT_EQUAL(2, cursor_move_line(l, 7, 10, -1));
    // Moving past the first/last line keeps the cursor.
    TEST_ASSERT_EQUAL(1, cursor_move_line(l, 1, 10, -1));
    TEST_ASSERT_EQUAL(6, cursor_move_line(l, 6, 10, 1));
}

void test_cursor_on_exactly_full_line() {
    const char* t = "abcdefgh";  // exactly 4+4 with cols=4
    auto l = wrap(t, strlen(t), 4, true);
    TEST_ASSERT_EQUAL(2, l.size());
    int line;
    size_t col;
    cursor_locate(l, 4, 4, line, col);
    TEST_ASSERT_EQUAL(1, line);
    TEST_ASSERT_EQUAL(0, col);
    cursor_locate(l, 8, 4, line, col);  // end of last full line stays on it
    TEST_ASSERT_EQUAL(1, line);
    TEST_ASSERT_EQUAL(4, col);
}

void test_fit() {
    TEST_ASSERT_EQUAL_STRING("short", fit("short", 10).c_str());
    TEST_ASSERT_EQUAL_STRING("a very...", fit("a very long title", 9).c_str());
    TEST_ASSERT_EQUAL_STRING("ab", fit("abcdef", 2).c_str());
}

// ---- Entry rules ------------------------------------------------------------

void test_finalize_default_titles() {
    Entry n;
    n.type = EntryType::Mnemonic;
    n.set(kFieldWords, "abandon about");
    TEST_ASSERT_NULL(finalize_entry(n));
    TEST_ASSERT_EQUAL_STRING("Seed phrase", n.title.c_str());
    Entry t;
    t.type = EntryType::Totp;
    t.set(kFieldTotpSecret, "JBSWY3DPEHPK3PXP");
    t.set(kFieldIssuer, "GitHub");
    TEST_ASSERT_NULL(finalize_entry(t));
    TEST_ASSERT_EQUAL_STRING("GitHub", t.title.c_str());
    Entry keep;
    keep.type = EntryType::Note;
    keep.title = "Mine";
    keep.set(kFieldBody, "text");
    TEST_ASSERT_NULL(finalize_entry(keep));
    TEST_ASSERT_EQUAL_STRING("Mine", keep.title.c_str());
}

void test_finalize_rejects_bad_totp_secret() {
    Entry t;
    t.type = EntryType::Totp;
    TEST_ASSERT_NOT_NULL(finalize_entry(t));  // empty
    t.set(kFieldTotpSecret, "not base32!");
    TEST_ASSERT_NOT_NULL(finalize_entry(t));
    t.set(kFieldTotpSecret, "jbsw y3dp ehpk 3pxp");  // spaces + lowercase are fine
    TEST_ASSERT_NULL(finalize_entry(t));
}

// Regression #31: Tab used to save entries with nothing in them.
void test_finalize_rejects_empty_defining_field() {
    const struct {
        EntryType type;
        uint8_t field;
    } cases[] = {{EntryType::Note, kFieldBody},
                 {EntryType::Mnemonic, kFieldWords},
                 {EntryType::Password, kFieldPassword},
                 {EntryType::Totp, kFieldTotpSecret}};
    for (auto& c : cases) {
        Entry e;
        e.type = c.type;
        e.title = "Has a title";
        e.set(kFieldNotes, "other fields do not count");
        e.set(kFieldUser, "alice");
        TEST_ASSERT_NOT_NULL(finalize_entry(e));  // missing
        e.set(c.field, "");
        TEST_ASSERT_NOT_NULL(finalize_entry(e));  // empty
        if (c.field == kFieldPassword) continue;  // verbatim: spaces are a value
        e.set(c.field, " \n\t ");
        TEST_ASSERT_NOT_NULL(finalize_entry(e));  // whitespace only
    }
}

void test_finalize_trims_but_keeps_passwords_verbatim() {
    Entry p;
    p.type = EntryType::Password;
    p.title = "  Email \n";
    p.set(kFieldUser, " alice ");
    p.set(kFieldPassword, " pa ss ");
    TEST_ASSERT_NULL(finalize_entry(p));
    TEST_ASSERT_EQUAL_STRING("Email", p.title.c_str());
    TEST_ASSERT_EQUAL_STRING("alice", p.get_or(kFieldUser).c_str());
    TEST_ASSERT_EQUAL_STRING(" pa ss ", p.get_or(kFieldPassword).c_str());

    Entry m;
    m.type = EntryType::Mnemonic;
    m.set(kFieldWords, "\n abandon about  ");
    m.set(kFieldBip39Pass, " 25th ");
    TEST_ASSERT_NULL(finalize_entry(m));
    TEST_ASSERT_EQUAL_STRING("abandon about", m.get_or(kFieldWords).c_str());
    TEST_ASSERT_EQUAL_STRING(" 25th ", m.get_or(kFieldBip39Pass).c_str());

    Entry blank_title;
    blank_title.type = EntryType::Note;
    blank_title.title = "   ";
    blank_title.set(kFieldBody, "x");
    TEST_ASSERT_NULL(finalize_entry(blank_title));
    TEST_ASSERT_EQUAL_STRING("Note", blank_title.title.c_str());
}

void test_finalize_totp_settings_and_short_secret_warning() {
    Entry t;
    t.type = EntryType::Totp;
    const char* warning = "stale";
    t.set(kFieldTotpSecret, "JBSWY3DPEHPK3PXP");  // 16 chars = 10 bytes
    TEST_ASSERT_NULL(finalize_entry(t, &warning));
    TEST_ASSERT_NULL(warning);
    t.set(kFieldTotpSecret, "JBSWY3DP");  // 8 chars: saved with a warning
    TEST_ASSERT_NULL(finalize_entry(t, &warning));
    TEST_ASSERT_NOT_NULL(warning);
    TEST_ASSERT_NULL(finalize_entry(t));  // warning pointer is optional

    t.set(kFieldTotpSecret, "JBSWY3DPEHPK3PXP");
    t.set(kFieldTotpDigits, "8");
    t.set(kFieldTotpPeriod, "60");
    TEST_ASSERT_NULL(finalize_entry(t));
    TEST_ASSERT_EQUAL_STRING("8", t.get_or(kFieldTotpDigits).c_str());
    TEST_ASSERT_EQUAL_STRING("60", t.get_or(kFieldTotpPeriod).c_str());
}

// Legacy/restored entries with odd settings still save; the stored values become
// the ones the code was already computed with, so codes do not change.
void test_finalize_normalises_totp_settings() {
    const struct {
        const char *digits, *period, *want_digits, *want_period;
    } cases[] = {{"7", "5", "6", "30"},   {"", "", "6", "30"},       {"8x", "301", "8", "30"},
                 {"abc", "10", "6", "10"}, {"08", "300", "8", "300"}};
    for (auto& c : cases) {
        Entry t;
        t.type = EntryType::Totp;
        t.set(kFieldTotpSecret, "JBSWY3DPEHPK3PXP");
        t.set(kFieldTotpDigits, c.digits);
        t.set(kFieldTotpPeriod, c.period);
        int digits;
        uint32_t period;
        totp_settings(t, digits, period);
        TEST_ASSERT_NULL(finalize_entry(t));
        TEST_ASSERT_EQUAL_STRING(c.want_digits, t.get_or(kFieldTotpDigits).c_str());
        TEST_ASSERT_EQUAL_STRING(c.want_period, t.get_or(kFieldTotpPeriod).c_str());
        TEST_ASSERT_EQUAL(atoi(c.want_digits), digits);
        TEST_ASSERT_EQUAL(atoi(c.want_period), int(period));
    }
    Entry missing;  // no settings stored at all
    int digits;
    uint32_t period;
    totp_settings(missing, digits, period);
    TEST_ASSERT_EQUAL(6, digits);
    TEST_ASSERT_EQUAL(30, int(period));
}

void test_finalize_keeps_multiline_text_and_space_passwords() {
    Entry n;
    n.type = EntryType::Note;
    n.set(kFieldBody, "  indented\n    code\n\n");
    TEST_ASSERT_NULL(finalize_entry(n));
    TEST_ASSERT_EQUAL_STRING("  indented\n    code\n\n", n.get_or(kFieldBody).c_str());

    Entry p;
    p.type = EntryType::Password;
    p.set(kFieldPassword, "    ");  // verbatim field: spaces are a value
    TEST_ASSERT_NULL(finalize_entry(p));
    TEST_ASSERT_EQUAL_STRING("    ", p.get_or(kFieldPassword).c_str());
}

void test_field_value_empty_and_defining_field() {
    TEST_ASSERT_TRUE(field_value_empty(kFieldPassword, ""));
    TEST_ASSERT_FALSE(field_value_empty(kFieldPassword, "  "));
    TEST_ASSERT_FALSE(field_value_empty(kFieldBip39Pass, " "));
    TEST_ASSERT_TRUE(field_value_empty(kFieldUser, " \t"));
    TEST_ASSERT_TRUE(field_value_empty(kFieldBody, "\n\n"));
    TEST_ASSERT_FALSE(field_value_empty(kFieldWords, " abandon "));
    TEST_ASSERT_EQUAL(kFieldBody, entry_defining_field(EntryType::Note));
    TEST_ASSERT_EQUAL(kFieldWords, entry_defining_field(EntryType::Mnemonic));
    TEST_ASSERT_EQUAL(kFieldPassword, entry_defining_field(EntryType::Password));
    TEST_ASSERT_EQUAL(kFieldTotpSecret, entry_defining_field(EntryType::Totp));
}

void test_dialog_messages_fit_the_screen() {
    auto lines = wrap(msg::kStorageSetup, strlen(msg::kStorageSetup), msg::kDialogCols);
    TEST_ASSERT_TRUE(int(lines.size()) <= msg::kDialogMaxLines);
    TEST_ASSERT_NOT_NULL(strstr(msg::kStorageSetup, "NOT affected"));
    TEST_ASSERT_TRUE(strlen(msg::kStorageSetupTitle) <= 20);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mnemonic_autoaccepts_unique_four_letter_prefix);
    RUN_TEST(test_mnemonic_full_phrase_completes_and_validates);
    RUN_TEST(test_mnemonic_short_words_need_enter_and_selection);
    RUN_TEST(test_mnemonic_uppercase_and_junk);
    RUN_TEST(test_mnemonic_backspace_into_previous_word);
    RUN_TEST(test_mnemonic_word_navigation_and_prefill);
    RUN_TEST(test_mnemonic_cannot_skip_an_empty_word);
    RUN_TEST(test_mnemonic_prev_word_keeps_a_complete_prefix);
    RUN_TEST(test_mnemonic_editing_a_prefilled_word);
    RUN_TEST(test_wrap_soft_and_hard);
    RUN_TEST(test_wrap_newlines_and_empty);
    RUN_TEST(test_cursor_locate_and_move);
    RUN_TEST(test_cursor_on_exactly_full_line);
    RUN_TEST(test_fit);
    RUN_TEST(test_dialog_messages_fit_the_screen);
    RUN_TEST(test_finalize_default_titles);
    RUN_TEST(test_finalize_rejects_bad_totp_secret);
    RUN_TEST(test_finalize_rejects_empty_defining_field);
    RUN_TEST(test_finalize_trims_but_keeps_passwords_verbatim);
    RUN_TEST(test_finalize_totp_settings_and_short_secret_warning);
    RUN_TEST(test_finalize_normalises_totp_settings);
    RUN_TEST(test_finalize_keeps_multiline_text_and_space_passwords);
    RUN_TEST(test_field_value_empty_and_defining_field);
    return UNITY_END();
}

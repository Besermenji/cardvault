#include "features/pwgen.h"

#include <cmath>
#include <cstring>

#include "crypto/rng.h"
#include "features/bip39.h"

namespace cv {

namespace {
const char* kLower = "abcdefghijklmnopqrstuvwxyz";
const char* kUpper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char* kDigits = "0123456789";
const char* kSymbols = "!@#$%^&*()-_=+[]{};:,.<>/?~";
const char* kAmbiguous = "0O1lI|";

SString filtered(const char* set, bool no_ambiguous) {
    SString s;
    for (const char* p = set; *p; p++)
        if (!no_ambiguous || !strchr(kAmbiguous, *p)) s += *p;
    return s;
}
}  // namespace

SString pwgen_charset(const PwOptions& o) {
    SString s;
    if (o.lower) s += filtered(kLower, o.no_ambiguous);
    if (o.upper) s += filtered(kUpper, o.no_ambiguous);
    if (o.digits) s += filtered(kDigits, o.no_ambiguous);
    if (o.symbols) s += filtered(kSymbols, o.no_ambiguous);
    return s;
}

double pwgen_password_bits(const PwOptions& o) {
    size_t n = pwgen_charset(o).size();
    return n ? o.length * std::log2(double(n)) : 0.0;
}

SString pwgen_password(const PwOptions& o) {
    SString all = pwgen_charset(o);
    SString out;
    if (all.empty() || o.length <= 0) return out;

    SVec<SString> classes;
    if (o.lower) classes.push_back(filtered(kLower, o.no_ambiguous));
    if (o.upper) classes.push_back(filtered(kUpper, o.no_ambiguous));
    if (o.digits) classes.push_back(filtered(kDigits, o.no_ambiguous));
    if (o.symbols) classes.push_back(filtered(kSymbols, o.no_ambiguous));

    // Retry until every selected class is present (cheap; keeps distribution
    // uniform over the set of valid passwords).
    bool need_all = o.length >= int(classes.size());
    for (;;) {
        wipe(out);
        for (int i = 0; i < o.length; i++) out += all[rng_uniform(uint32_t(all.size()))];
        if (!need_all) break;
        bool ok = true;
        for (auto& c : classes)
            if (out.find_first_of(c.c_str()) == SString::npos) ok = false;
        if (ok) break;
    }
    return out;
}

SString pwgen_words(int count, char sep) {
    SString out;
    for (int i = 0; i < count; i++) {
        if (i) out += sep;
        out += kBip39Words[rng_uniform(2048)];
    }
    return out;
}

namespace {
const char* const kKeyRows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
const char* const kWordSeps = " -_.,";

char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_sep(char c) { return c && strchr(kWordSeps, c); }

// True if a and b (already lowercased) are neighbours on one keyboard row.
bool keyboard_neighbours(char a, char b) {
    if (!a || !b) return false;
    for (const char* row : kKeyRows) {
        const char* pa = strchr(row, a);
        const char* pb = strchr(row, b);
        if (pa && pb && (pa - pb == 1 || pb - pa == 1)) return true;
    }
    return false;
}

// Text made only of lowercase BIP39 words separated by spaces or hyphens (what
// the generator produces): 11 bits per distinct word. Returns false for any
// other shape. Works in fixed stack buffers that are wiped before returning,
// so no copy of a word outlives the call.
bool bip39_words_bits(const SString& pass, double& bits) {
    constexpr size_t kMaxWord = 8;  // longest BIP39 word
    constexpr size_t kMaxWords = 64;
    char word[kMaxWord + 1] = {};
    int seen[kMaxWords];
    size_t wlen = 0, nseen = 0;
    bool ok = true;
    auto flush = [&]() {
        if (!wlen) return;
        word[wlen] = 0;
        int idx = bip39_index(word);
        wipe(word, sizeof word);
        wlen = 0;
        if (idx < 0) {
            ok = false;
            return;
        }
        for (size_t k = 0; k < nseen; k++)
            if (seen[k] == idx) return;
        if (nseen == kMaxWords) {
            ok = false;
            return;
        }
        seen[nseen++] = idx;
    };
    for (size_t i = 0; ok && i < pass.size(); i++) {
        char c = pass[i];
        if (is_lower(c)) {
            if (wlen == kMaxWord)
                ok = false;
            else
                word[wlen++] = c;
        } else if (c == ' ' || c == '-') {
            flush();
        } else {
            ok = false;
        }
    }
    if (ok) flush();
    ok = ok && nseen > 0;
    if (ok) bits = 11.0 * double(nseen);
    wipe(word, sizeof word);
    wipe(seen, sizeof seen);
    return ok;
}

// Lowercase words, optionally split by " -_.,", optionally a capital at the
// start of a word, optionally a digit/symbol suffix of up to 6 ("Summer-time-2024!").
// The most common human pattern, so it gets a hard cap.
bool prose_like(const SString& pass) {
    size_t end = pass.size();
    size_t suffix = 0;
    while (end > 0 && !is_lower(pass[end - 1]) && !is_upper(pass[end - 1]) && suffix < 6) {
        end--;
        suffix++;
    }
    bool letters = false;
    for (size_t i = 0; i < end; i++) {
        char c = pass[i];
        if (is_lower(c)) {
            letters = true;
        } else if (is_upper(c)) {
            if (i > 0 && !is_sep(pass[i - 1])) return false;
            letters = true;
        } else if (!is_sep(c)) {
            return false;
        }
    }
    return letters;
}
}  // namespace

double passphrase_strength_bits(const SString& pass) {
    if (pass.empty()) return 0;
    double words_bits;
    if (bip39_words_bits(pass, words_bits)) return words_bits;

    bool lo = false, up = false, di = false, sy = false, sp = false;
    for (char c : pass) {
        if (is_lower(c))
            lo = true;
        else if (is_upper(c))
            up = true;
        else if (c >= '0' && c <= '9')
            di = true;
        else if (c == ' ')
            sp = true;
        else
            sy = true;
    }
    int pool = (lo ? 26 : 0) + (up ? 26 : 0) + (di ? 10 : 0) + (sy ? 32 : 0) + (sp ? 1 : 0);

    // Characters an attacker gets almost for free, ignoring case: repeats,
    // runs (abc / cba), keyboard walks, and any 3-gram that appeared earlier.
    const size_t n = pass.size();
    SVec<char> cheap(n, 0);
    for (size_t i = 1; i < n; i++) {
        char a = lower_ascii(pass[i - 1]), b = lower_ascii(pass[i]);
        if (b == a || b == a + 1 || b == a - 1 || keyboard_neighbours(a, b)) cheap[i] = 1;
    }
    for (size_t i = 2; i < n; i++) {
        for (size_t j = 0; j + 3 <= i; j++) {
            if (pass[j] == pass[i - 2] && pass[j + 1] == pass[i - 1] && pass[j + 2] == pass[i]) {
                cheap[i - 2] = cheap[i - 1] = cheap[i] = 1;
                break;
            }
        }
    }
    double effective_len = 0;
    for (size_t i = 0; i < n; i++) effective_len += cheap[i] ? 0.1 : 1.0;
    // Human-chosen text carries far less entropy than random text; scale down.
    double bits = effective_len * std::log2(double(pool)) * 0.6;
    if (prose_like(pass) && bits > 50) bits = 50;
    return bits;
}

}  // namespace cv

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

char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

// True if a and b are neighbours on one keyboard row (either direction).
bool keyboard_neighbours(char a, char b) {
    a = lower_ascii(a);
    b = lower_ascii(b);
    for (const char* row : kKeyRows) {
        const char* pa = strchr(row, a);
        const char* pb = strchr(row, b);
        if (a && b && pa && pb && (pa - pb == 1 || pb - pa == 1)) return true;
    }
    return false;
}

// Only lowercase words separated by spaces/hyphens, all of them BIP39 words:
// score 11 bits per distinct word. Returns false if the text isn't of that shape.
bool bip39_words_bits(const SString& pass, double& bits) {
    SVec<SString> seen;
    SString word;
    auto flush = [&]() {
        if (word.empty()) return true;
        if (bip39_index(word.c_str()) < 0) return false;
        for (auto& w : seen)
            if (w == word) {
                word.clear();
                return true;
            }
        seen.push_back(word);
        word.clear();
        return true;
    };
    for (char c : pass) {
        if (c >= 'a' && c <= 'z') {
            word += c;
        } else if (c == ' ' || c == '-') {
            if (!flush()) return false;
        } else {
            return false;
        }
    }
    if (!flush() || seen.empty()) return false;
    bits = 11.0 * double(seen.size());
    return true;
}
}  // namespace

double passphrase_strength_bits(const SString& pass) {
    if (pass.empty()) return 0;
    double words_bits;
    if (bip39_words_bits(pass, words_bits)) return words_bits;

    bool lo = false, up = false, di = false, sy = false, sp = false;
    for (char c : pass) {
        if (c >= 'a' && c <= 'z')
            lo = true;
        else if (c >= 'A' && c <= 'Z')
            up = true;
        else if (c >= '0' && c <= '9')
            di = true;
        else if (c == ' ')
            sp = true;
        else
            sy = true;
    }
    int pool = (lo ? 26 : 0) + (up ? 26 : 0) + (di ? 10 : 0) + (sy ? 32 : 0) + (sp ? 1 : 0);

    // Characters an attacker gets almost for free: repeats, runs (abc / cba),
    // keyboard walks, and any 3-gram that already appeared earlier.
    const size_t n = pass.size();
    SVec<char> cheap(n, 0);
    for (size_t i = 1; i < n; i++) {
        char a = pass[i - 1], b = pass[i];
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

    // Plain lowercase prose is the easiest class to guess.
    bool prose = true;
    for (char c : pass)
        if (!((c >= 'a' && c <= 'z') || c == ' ')) prose = false;
    if (prose && bits > 50) bits = 50;
    return bits;
}

}  // namespace cv

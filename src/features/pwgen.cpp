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

double passphrase_strength_bits(const SString& pass) {
    if (pass.empty()) return 0;
    bool lo = false, up = false, di = false, sy = false, sp = false;
    int repeats = 0;
    for (size_t i = 0; i < pass.size(); i++) {
        char c = pass[i];
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
        if (i > 0 && (pass[i] == pass[i - 1] || pass[i] == pass[i - 1] + 1)) repeats++;
    }
    int pool = (lo ? 26 : 0) + (up ? 26 : 0) + (di ? 10 : 0) + (sy ? 32 : 0) + (sp ? 1 : 0);
    double effective_len = double(pass.size()) - repeats * 0.75;
    if (effective_len < 1) effective_len = 1;
    // Human-chosen text carries far less entropy than random text; scale down.
    return effective_len * std::log2(double(pool)) * 0.6;
}

}  // namespace cv

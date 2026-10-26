// Password / passphrase generation and strength estimation.
#pragma once

#include "crypto/secure.h"

namespace cv {

struct PwOptions {
    int length = 20;
    bool lower = true;
    bool upper = true;
    bool digits = true;
    bool symbols = true;
    bool no_ambiguous = false;  // drop 0O1lI| etc.
};

// Characters allowed by the options (empty if none selected).
SString pwgen_charset(const PwOptions& opt);
// Random password; guarantees at least one character from each selected class
// when length allows.
SString pwgen_password(const PwOptions& opt);
double pwgen_password_bits(const PwOptions& opt);
// Random passphrase of `count` BIP39 words (11 bits each) joined by `sep`.
SString pwgen_words(int count, char sep);
inline double pwgen_words_bits(int count) { return 11.0 * count; }

// Rough strength estimate (bits) of a user-chosen passphrase. Display only:
// the vault core enforces just the length policy. Penalises repeats, runs,
// keyboard walks and repeated chunks (case-insensitive) and caps word-like
// prose ("Summer-time-2024!") at 50 bits. Text made only of BIP39 words gets
// 11 bits per distinct word: that assumes the words were generated at random,
// so hand-picked BIP39 words are overrated (deliberate; the hint recommends
// the generator).
double passphrase_strength_bits(const SString& pass);

}  // namespace cv

// BIP39 mnemonic helpers (English wordlist): lookup, autocomplete,
// checksum validation and generation from entropy.
#pragma once

#include <cstddef>
#include <cstdint>

#include "crypto/secure.h"

namespace cv {

extern const char* const kBip39Words[2048];

enum class Bip39Status {
    Ok,
    BadWordCount,  // not 12/15/18/21/24
    UnknownWord,   // a word is not in the list
    BadChecksum,
};

// Index of an exact word, or -1.
int bip39_index(const char* word);
// Fills up to `max` word indices that start with `prefix`; returns the total
// number of matches (may exceed max).
int bip39_complete(const char* prefix, int* out, int max);
// Validates a space-separated mnemonic. If bad_word is non-null it receives the
// 0-based position of the first unknown word.
Bip39Status bip39_validate(const SString& mnemonic, int* bad_word = nullptr);
// Builds a mnemonic from 16/20/24/28/32 bytes of entropy.
bool bip39_from_entropy(const uint8_t* entropy, size_t len, SString& out);
// Generates a new random mnemonic with word_count words.
bool bip39_generate(int word_count, SString& out);
bool bip39_valid_word_count(int n);
// Splits a space-separated mnemonic into words.
SVec<SString> bip39_split(const SString& mnemonic);

}  // namespace cv

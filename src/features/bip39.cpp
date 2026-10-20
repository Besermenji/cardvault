#include "features/bip39.h"

#include <cstring>

#include "crypto/rng.h"
#include "crypto/sha.h"

namespace cv {

namespace {
// First index whose word is >= key (wordlist is sorted).
int lower_bound(const char* key) {
    int lo = 0, hi = 2048;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (strcmp(kBip39Words[mid], key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}
}  // namespace

bool bip39_valid_word_count(int n) { return n >= 12 && n <= 24 && n % 3 == 0; }

int bip39_index(const char* word) {
    int i = lower_bound(word);
    return (i < 2048 && strcmp(kBip39Words[i], word) == 0) ? i : -1;
}

int bip39_complete(const char* prefix, int* out, int max) {
    size_t plen = strlen(prefix);
    if (plen == 0) return 0;
    int count = 0;
    for (int i = lower_bound(prefix); i < 2048; i++) {
        if (strncmp(kBip39Words[i], prefix, plen) != 0) break;
        if (count < max) out[count] = i;
        count++;
    }
    return count;
}

SVec<SString> bip39_split(const SString& mnemonic) {
    SVec<SString> words;
    SString cur;
    for (char c : mnemonic) {
        if (c == ' ' || c == '\n' || c == '\t') {
            if (!cur.empty()) words.push_back(cur);
            wipe(cur);
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) words.push_back(cur);
    wipe(cur);
    return words;
}

Bip39Status bip39_validate(const SString& mnemonic, int* bad_word) {
    SVec<SString> words = bip39_split(mnemonic);
    int n = int(words.size());
    if (!bip39_valid_word_count(n)) return Bip39Status::BadWordCount;

    // Pack 11-bit indices into a bit buffer: ENT bits + CS bits (CS = ENT/32).
    uint8_t bits[33] = {0};
    for (int w = 0; w < n; w++) {
        int idx = bip39_index(words[w].c_str());
        if (idx < 0) {
            if (bad_word) *bad_word = w;
            wipe(bits, sizeof bits);
            return Bip39Status::UnknownWord;
        }
        for (int b = 0; b < 11; b++) {
            if (idx & (1 << (10 - b))) {
                int pos = w * 11 + b;
                bits[pos / 8] |= uint8_t(0x80 >> (pos % 8));
            }
        }
    }
    int total_bits = n * 11;
    int cs_bits = total_bits / 33;
    int ent_bytes = (total_bits - cs_bits) / 8;
    uint8_t hash[32];
    sha256(bits, ent_bytes, hash);
    bool ok = true;
    for (int i = 0; i < cs_bits; i++) {
        int pos = ent_bytes * 8 + i;
        bool want = hash[0] & (0x80 >> i);
        bool got = bits[pos / 8] & (0x80 >> (pos % 8));
        if (want != got) ok = false;
    }
    wipe(bits, sizeof bits);
    wipe(hash, sizeof hash);
    return ok ? Bip39Status::Ok : Bip39Status::BadChecksum;
}

bool bip39_from_entropy(const uint8_t* entropy, size_t len, SString& out) {
    if (len < 16 || len > 32 || len % 4 != 0) return false;
    uint8_t buf[33];
    memcpy(buf, entropy, len);
    uint8_t hash[32];
    sha256(entropy, len, hash);
    buf[len] = hash[0];
    int words = int(len * 8 + len / 4) / 11;
    wipe(out);
    for (int w = 0; w < words; w++) {
        int idx = 0;
        for (int b = 0; b < 11; b++) {
            int pos = w * 11 + b;
            idx = (idx << 1) | ((buf[pos / 8] >> (7 - pos % 8)) & 1);
        }
        if (w) out += ' ';
        out += kBip39Words[idx];
    }
    wipe(buf, sizeof buf);
    wipe(hash, sizeof hash);
    return true;
}

bool bip39_generate(int word_count, SString& out) {
    if (!bip39_valid_word_count(word_count)) return false;
    size_t len = size_t(word_count) * 4 / 3;  // 12 -> 16 bytes ... 24 -> 32 bytes
    uint8_t ent[32];
    rng_fill(ent, len);
    bool ok = bip39_from_entropy(ent, len, out);
    wipe(ent, sizeof ent);
    return ok;
}

}  // namespace cv

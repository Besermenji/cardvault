#include "core/entry_rules.h"

#include <cctype>

#include "features/base32.h"

namespace cv {

namespace {

bool is_space(char c) { return isspace(uint8_t(c)) != 0; }

// Strips leading and trailing whitespace in place.
void trim(SString& s) {
    size_t end = s.size();
    while (end > 0 && is_space(s[end - 1])) end--;
    size_t start = 0;
    while (start < end && is_space(s[start])) start++;
    if (end < s.size()) s.erase(end);
    if (start > 0) s.erase(0, start);
}

bool blank(const SString& s) {
    for (char c : s)
        if (!is_space(c)) return false;
    return true;
}

// Passwords and BIP39 passphrases are kept exactly as typed: spaces may be part of the secret.
bool keep_verbatim(uint8_t key) { return key == kFieldPassword || key == kFieldBip39Pass; }

// The field an entry of this type is useless without, and the message when it is empty.
uint8_t defining_field(EntryType t, const char*& missing) {
    switch (t) {
        case EntryType::Note: missing = "Text is empty"; return kFieldBody;
        case EntryType::Mnemonic: missing = "Seed words are empty"; return kFieldWords;
        case EntryType::Password: missing = "Password is empty"; return kFieldPassword;
        case EntryType::Totp: missing = "Secret is empty"; return kFieldTotpSecret;
    }
    missing = "Entry is empty";
    return 0;
}

// Parses an all-digit string; returns -1 on anything else.
int parse_uint(const SString& s) {
    if (s.empty() || s.size() > 4) return -1;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return -1;
        v = v * 10 + (c - '0');
    }
    return v;
}

const char* check_totp(const Entry& draft, const char** warning) {
    SBytes key;
    bool ok = base32_decode(draft.get_or(kFieldTotpSecret), key) && !key.empty();
    size_t len = key.size();
    wipe(key);
    if (!ok) return "Secret is not valid base32";
    if (len < 10 && warning) *warning = "Short secret (<16 chars)";
    int digits = parse_uint(draft.get_or(kFieldTotpDigits, "6"));
    if (digits != 6 && digits != 8) return "Digits must be 6 or 8";
    int period = parse_uint(draft.get_or(kFieldTotpPeriod, "30"));
    if (period < 10 || period > 300) return "Period must be 10-300 s";
    return nullptr;
}

}  // namespace

const char* finalize_entry(Entry& draft, const char** warning) {
    if (warning) *warning = nullptr;
    trim(draft.title);
    for (auto& f : draft.fields)
        if (!keep_verbatim(f.key)) trim(f.value);

    const char* missing;
    uint8_t key = defining_field(draft.type, missing);
    const SString* value = draft.get(key);
    if (!value || blank(*value)) return missing;

    if (draft.type == EntryType::Totp)
        if (const char* err = check_totp(draft, warning)) return err;

    if (draft.title.empty()) {
        SString issuer = draft.get_or(kFieldIssuer);
        if (draft.type == EntryType::Totp && !issuer.empty())
            draft.title = issuer;
        else
            draft.title = entry_type_name(draft.type);
        wipe(issuer);
    }
    return nullptr;
}

}  // namespace cv

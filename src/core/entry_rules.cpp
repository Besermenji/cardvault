#include "core/entry_rules.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

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

// Passwords and BIP39 passphrases are secrets where spaces may matter; multi-line
// text may be indented on purpose. All are stored exactly as typed.
bool keep_verbatim(uint8_t key) {
    return key == kFieldPassword || key == kFieldBip39Pass || field_is_multiline(key);
}

const char* missing_message(EntryType t) {
    switch (t) {
        case EntryType::Note: return "Text is empty";
        case EntryType::Mnemonic: return "Seed words are empty";
        case EntryType::Password: return "Password is empty";
        case EntryType::Totp: return "Secret is empty";
    }
    return "Entry is empty";
}

int int_field(const Entry& e, uint8_t key, const char* fallback) {
    const SString* v = e.get(key);
    return atoi(v ? v->c_str() : fallback);
}

const char* check_totp(Entry& draft, const char** warning) {
    SBytes key;
    const SString* secret = draft.get(kFieldTotpSecret);
    bool ok = secret && base32_decode(*secret, key) && !key.empty();
    size_t len = key.size();
    wipe(key);
    if (!ok) return "Secret is not valid base32";
    if (len < 10 && warning) *warning = "Short secret (<16 chars)";
    // Store the settings the code is actually computed with.
    int digits;
    uint32_t period;
    totp_settings(draft, digits, period);
    char buf[8];
    snprintf(buf, sizeof buf, "%d", digits);
    draft.set(kFieldTotpDigits, buf);
    snprintf(buf, sizeof buf, "%u", unsigned(period));
    draft.set(kFieldTotpPeriod, buf);
    return nullptr;
}

}  // namespace

uint8_t entry_defining_field(EntryType t) {
    switch (t) {
        case EntryType::Note: return kFieldBody;
        case EntryType::Mnemonic: return kFieldWords;
        case EntryType::Password: return kFieldPassword;
        case EntryType::Totp: return kFieldTotpSecret;
    }
    return 0;
}

bool field_value_empty(uint8_t key, const SString& value) {
    if (key == kFieldPassword || key == kFieldBip39Pass) return value.empty();
    for (char c : value)
        if (!is_space(c)) return false;
    return true;
}

void totp_settings(const Entry& e, int& digits, uint32_t& period) {
    digits = int_field(e, kFieldTotpDigits, "6") == 8 ? 8 : 6;
    int p = int_field(e, kFieldTotpPeriod, "30");
    period = p >= 10 && p <= 300 ? uint32_t(p) : 30;
}

const char* finalize_entry(Entry& draft, const char** warning) {
    if (warning) *warning = nullptr;
    trim(draft.title);
    for (auto& f : draft.fields)
        if (!keep_verbatim(f.key)) trim(f.value);

    uint8_t key = entry_defining_field(draft.type);
    const SString* value = draft.get(key);
    if (!value || field_value_empty(key, *value)) return missing_message(draft.type);

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

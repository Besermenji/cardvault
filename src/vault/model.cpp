#include "vault/model.h"

namespace cv {

const SString* Entry::get(uint8_t key) const {
    for (auto& f : fields)
        if (f.key == key) return &f.value;
    return nullptr;
}

SString Entry::get_or(uint8_t key, const char* fallback) const {
    const SString* v = get(key);
    return v ? *v : SString(fallback);
}

void Entry::set(uint8_t key, const SString& value) {
    for (auto& f : fields) {
        if (f.key == key) {
            wipe(f.value);
            f.value = value;
            return;
        }
    }
    fields.push_back(Field{key, value});
}

void Entry::clear() {
    wipe(title);
    for (auto& f : fields) wipe(f.value);
    fields.clear();
    fields.shrink_to_fit();
}

Entry* VaultData::find(uint32_t id) {
    for (auto& e : entries)
        if (e.id == id) return &e;
    return nullptr;
}

uint32_t VaultData::add(Entry e) {
    e.id = next_id++;
    entries.push_back(std::move(e));
    return entries.back().id;
}

bool VaultData::remove(uint32_t id) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->id == id) {
            it->clear();
            entries.erase(it);
            return true;
        }
    }
    return false;
}

void VaultData::clear() {
    for (auto& e : entries) e.clear();
    entries.clear();
    entries.shrink_to_fit();
    next_id = 1;
}

const char* entry_type_name(EntryType t) {
    switch (t) {
        case EntryType::Note: return "Note";
        case EntryType::Mnemonic: return "Seed phrase";
        case EntryType::Password: return "Password";
        case EntryType::Totp: return "2FA (TOTP)";
    }
    return "?";
}

const char* field_label(uint8_t key) {
    switch (key) {
        case kFieldBody: return "Text";
        case kFieldWords: return "Words";
        case kFieldBip39Pass: return "BIP39 passphrase";
        case kFieldUser: return "Username";
        case kFieldPassword: return "Password";
        case kFieldUrl: return "URL";
        case kFieldNotes: return "Notes";
        case kFieldTotpSecret: return "Secret (base32)";
        case kFieldTotpDigits: return "Digits";
        case kFieldTotpPeriod: return "Period (s)";
        case kFieldIssuer: return "Issuer";
    }
    return "Field";
}

bool field_is_secret(uint8_t key) {
    return key == kFieldBody || key == kFieldWords || key == kFieldBip39Pass ||
           key == kFieldPassword || key == kFieldTotpSecret || key == kFieldNotes;
}

bool field_is_multiline(uint8_t key) { return key == kFieldBody || key == kFieldNotes; }

const uint8_t* entry_type_fields(EntryType t, int& count) {
    static const uint8_t note[] = {kFieldBody};
    static const uint8_t mnemonic[] = {kFieldWords, kFieldBip39Pass, kFieldNotes};
    static const uint8_t password[] = {kFieldUser, kFieldPassword, kFieldUrl, kFieldNotes};
    static const uint8_t totp[] = {kFieldIssuer, kFieldTotpSecret, kFieldTotpDigits,
                                   kFieldTotpPeriod};
    switch (t) {
        case EntryType::Note: count = 1; return note;
        case EntryType::Mnemonic: count = 3; return mnemonic;
        case EntryType::Password: count = 4; return password;
        case EntryType::Totp: count = 4; return totp;
    }
    count = 0;
    return nullptr;
}

}  // namespace cv

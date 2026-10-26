// In-memory vault contents. Every string uses wiping allocators.
#pragma once

#include <cstdint>

#include "crypto/secure.h"

namespace cv {

enum class EntryType : uint8_t {
    Note = 1,
    Mnemonic = 2,
    Password = 3,
    Totp = 4,
};

enum FieldKey : uint8_t {
    kFieldBody = 1,        // note text
    kFieldWords = 2,       // mnemonic words, space separated
    kFieldBip39Pass = 3,   // optional BIP39 passphrase ("25th word")
    kFieldUser = 4,
    kFieldPassword = 5,
    kFieldUrl = 6,
    kFieldNotes = 7,
    kFieldTotpSecret = 8,  // base32
    kFieldTotpDigits = 9,  // "6" / "8"
    kFieldTotpPeriod = 10, // seconds, "30"
    kFieldIssuer = 11,
};

struct Field {
    uint8_t key = 0;
    SString value;
};

struct Entry {
    uint32_t id = 0;
    EntryType type = EntryType::Note;
    uint32_t created = 0;  // unix time, 0 if unknown
    uint32_t updated = 0;
    SString title;
    SVec<Field> fields;

    const SString* get(uint8_t key) const;
    SString get_or(uint8_t key, const char* fallback = "") const;
    // The stored value, or an empty string: no copy, so use it for secrets.
    const SString& value(uint8_t key) const;
    void set(uint8_t key, const SString& value);
    void clear();
};

struct VaultData {
    uint32_t next_id = 1;
    SVec<Entry> entries;

    Entry* find(uint32_t id);
    // Assigns a fresh id and stores a copy of the entry; returns the id.
    uint32_t add(const Entry& e);
    bool remove(uint32_t id);
    void clear();
};

const char* entry_type_name(EntryType t);
const char* field_label(uint8_t key);
bool field_is_secret(uint8_t key);
bool field_is_multiline(uint8_t key);
// The fields shown/edited for each entry type, in display order.
const uint8_t* entry_type_fields(EntryType t, int& count);

}  // namespace cv

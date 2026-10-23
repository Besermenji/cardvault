// Validation and normalisation applied before an entry is saved.
#pragma once

#include "vault/model.h"

namespace cv {

// Trims whitespace (not in passwords, BIP39 passphrases or multi-line text),
// rejects an entry whose defining field is empty, normalises TOTP settings and
// fills defaults (title). Returns nullptr when the entry can be saved,
// otherwise a user message. `warning` receives an advisory message for
// savable entries.
const char* finalize_entry(Entry& draft, const char** warning = nullptr);

// The field an entry of this type cannot be saved without.
uint8_t entry_defining_field(EntryType t);

// Whether a value counts as empty for this field: passwords and BIP39
// passphrases are verbatim, so only "" is empty; elsewhere whitespace-only is.
bool field_value_empty(uint8_t key, const SString& value);

// TOTP digits (6/8) and period (10-300 s); invalid values fall back to 6 / 30.
void totp_settings(const Entry& e, int& digits, uint32_t& period);

}  // namespace cv

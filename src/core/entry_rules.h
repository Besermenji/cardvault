// Validation and normalisation applied before an entry is saved.
#pragma once

#include "vault/model.h"

namespace cv {

// Trims whitespace (except in passwords and BIP39 passphrases), rejects an
// entry whose defining field is empty, validates TOTP settings and fills
// defaults (title). Returns nullptr when the entry can be saved, otherwise a
// user message. `warning` receives an advisory message for savable entries.
const char* finalize_entry(Entry& draft, const char** warning = nullptr);

}  // namespace cv

// Validation and normalisation applied before an entry is saved.
#pragma once

#include "vault/model.h"

namespace cv {

// Fills defaults (title) and validates type-specific fields.
// Returns nullptr when the entry can be saved, otherwise a user message.
const char* finalize_entry(Entry& draft);

}  // namespace cv

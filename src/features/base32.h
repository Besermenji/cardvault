// RFC 4648 base32 decoding (case-insensitive, ignores spaces, dashes, padding).
#pragma once

#include "crypto/secure.h"

namespace cv {

bool base32_decode(const SString& in, SBytes& out);

}  // namespace cv

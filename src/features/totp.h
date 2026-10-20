// HOTP (RFC 4226) / TOTP (RFC 6238) with HMAC-SHA1.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cv {

uint32_t hotp(const uint8_t* key, size_t key_len, uint64_t counter, int digits);
uint32_t totp(const uint8_t* key, size_t key_len, uint64_t unix_time, uint32_t period,
              int digits);
// Writes the code zero-padded to `digits` characters (buf must hold digits + 1).
void totp_format(uint32_t code, int digits, char* buf);

}  // namespace cv

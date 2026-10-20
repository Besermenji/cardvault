// Small, portable SHA-1 / SHA-256 / HMAC-SHA1 (TOTP and BIP39 checksums).
#pragma once

#include <cstddef>
#include <cstdint>

namespace cv {

void sha1(const uint8_t* msg, size_t len, uint8_t out[20]);
void sha256(const uint8_t* msg, size_t len, uint8_t out[32]);
void hmac_sha1(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
               uint8_t out[20]);

}  // namespace cv

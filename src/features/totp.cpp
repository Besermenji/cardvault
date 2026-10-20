#include "features/totp.h"

#include "crypto/secure.h"
#include "crypto/sha.h"

namespace cv {

uint32_t hotp(const uint8_t* key, size_t key_len, uint64_t counter, int digits) {
    uint8_t msg[8];
    for (int i = 7; i >= 0; i--) {
        msg[i] = uint8_t(counter);
        counter >>= 8;
    }
    uint8_t mac[20];
    hmac_sha1(key, key_len, msg, sizeof msg, mac);
    int off = mac[19] & 0x0f;
    uint32_t bin = (uint32_t(mac[off] & 0x7f) << 24) | (uint32_t(mac[off + 1]) << 16) |
                   (uint32_t(mac[off + 2]) << 8) | mac[off + 3];
    wipe(mac, sizeof mac);
    uint32_t mod = 1;
    for (int i = 0; i < digits; i++) mod *= 10;
    return bin % mod;
}

uint32_t totp(const uint8_t* key, size_t key_len, uint64_t unix_time, uint32_t period,
              int digits) {
    if (period == 0) period = 30;
    return hotp(key, key_len, unix_time / period, digits);
}

void totp_format(uint32_t code, int digits, char* buf) {
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = char('0' + code % 10);
        code /= 10;
    }
    buf[digits] = '\0';
}

}  // namespace cv

// Header fields that are checked structurally (no passphrase, no KDF run),
// shared by the vault and navigation suites.
// Layout: 4 version, 5 kdf id, 6-7 reserved, 8-11 mem, 12-15 passes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "vault/format.h"

namespace cv {
namespace test {

inline void put_le32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; i++) b[off + i] = uint8_t(v >> (8 * i));
}

struct HeaderTamper {
    const char* what;
    std::function<void(std::vector<uint8_t>&)> apply;
    VaultError expect;
};

inline std::vector<HeaderTamper> header_tampers() {
    using B = std::vector<uint8_t>;
    return {
        {"version", [](B& b) { b[4] ^= 1; }, VaultError::UnsupportedVersion},
        {"version 0", [](B& b) { b[4] = 0; }, VaultError::UnsupportedVersion},
        {"kdf id", [](B& b) { b[5] ^= 1; }, VaultError::BadParams},
        {"kdf id 0", [](B& b) { b[5] = 0; }, VaultError::BadParams},
        {"mem over cap", [](B& b) { put_le32(b, 8, kMaxKdfMemKib + 8); }, VaultError::BadParams},
        {"mem under min", [](B& b) { put_le32(b, 8, kMinKdfMemKib - 8); }, VaultError::BadParams},
        {"mem not x8", [](B& b) { put_le32(b, 8, 4097); }, VaultError::BadParams},
        {"mem huge", [](B& b) { put_le32(b, 8, 0xFFFFFFF8u); }, VaultError::BadParams},
        {"passes over cap", [](B& b) { put_le32(b, 12, kMaxKdfPasses + 1); }, VaultError::BadParams},
        {"passes under min", [](B& b) { put_le32(b, 12, kMinKdfPasses - 1); }, VaultError::BadParams},
        {"passes huge", [](B& b) { put_le32(b, 12, 0xFFFFFFFFu); }, VaultError::BadParams},
        {"work over cap",
         [](B& b) {
             put_le32(b, 8, kMaxKdfMemKib);
             put_le32(b, 12, 25);
         },
         VaultError::BadParams},
    };
}

}  // namespace test
}  // namespace cv

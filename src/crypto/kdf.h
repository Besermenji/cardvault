// Passphrase -> 32-byte key using Argon2id.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cv {

struct KdfParams {
    uint32_t mem_kib;  // Argon2 memory in KiB (= number of 1 KiB blocks)
    uint32_t passes;   // Argon2 time cost
};

constexpr size_t kSaltSize = 16;
constexpr uint32_t kMinKdfMemKib = 64;
constexpr uint32_t kMaxKdfMemKib = 4096;
constexpr uint32_t kMinKdfPasses = 3;
constexpr uint32_t kMaxKdfPasses = 1000;

bool kdf_params_valid(const KdfParams& p);

// Returns false if parameters are invalid or the work area cannot be allocated.
bool kdf_derive(uint8_t out[32], const uint8_t* pass, size_t pass_len,
                const uint8_t salt[kSaltSize], const KdfParams& p);

}  // namespace cv

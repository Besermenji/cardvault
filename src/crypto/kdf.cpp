#include "crypto/kdf.h"

#include <cstdlib>

#include "crypto/secure.h"

namespace cv {

bool kdf_params_valid(const KdfParams& p) {
    return p.mem_kib >= kMinKdfMemKib && p.mem_kib <= kMaxKdfMemKib && p.mem_kib % 8 == 0 &&
           p.passes >= kMinKdfPasses && p.passes <= kMaxKdfPasses;
}

bool kdf_derive(uint8_t out[32], const uint8_t* pass, size_t pass_len,
                const uint8_t salt[kSaltSize], const KdfParams& p) {
    if (!kdf_params_valid(p)) return false;
    size_t work_size = size_t(p.mem_kib) * 1024;
    void* work = malloc(work_size);
    if (!work) return false;

    crypto_argon2_config cfg = {CRYPTO_ARGON2_ID, p.mem_kib, p.passes, 1};
    crypto_argon2_inputs in = {pass, salt, uint32_t(pass_len), uint32_t(kSaltSize)};
    crypto_argon2(out, 32, work, cfg, in, crypto_argon2_no_extras);

    wipe(work, work_size);
    free(work);
    return true;
}

}  // namespace cv

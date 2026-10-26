// On-disk vault format.
//
//   header (104 bytes)
//     magic "CVLT" | version u8 | kdf_alg u8 | reserved u16 |
//     kdf_mem_kib u32 | kdf_passes u32 | salt[16]           <- 32 bytes "params"
//     wrap_nonce[24] | wrapped_dek[32] | wrap_mac[16]
//   body
//     nonce[24] | mac[16] | ciphertext
//
// KEK = Argon2id(passphrase, salt, params). The KEK encrypts the random DEK
// (XChaCha20-Poly1305, AD = params). The DEK encrypts the padded payload
// (XChaCha20-Poly1305, AD = full header). All integers are little-endian.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "crypto/kdf.h"
#include "crypto/secure.h"

namespace cv {

constexpr uint8_t kFormatVersion = 1;
constexpr uint8_t kKdfArgon2id = 2;
constexpr size_t kParamsSize = 32;
constexpr size_t kHeaderSize = 104;
constexpr size_t kNonceSize = 24;
constexpr size_t kMacSize = 16;
constexpr size_t kMaxPayload = 48 * 1024;
constexpr size_t kPadBlock = 256;
constexpr size_t kMinPassphraseLen = 8;
constexpr size_t kMaxPassphraseLen = 128;

enum class VaultError {
    Ok,
    Truncated,
    BadMagic,
    UnsupportedVersion,
    BadParams,
    WrongPassphrase,
    Corrupt,
    TooLarge,
    OutOfMemory,
    Io,
    NotOpen,
    WeakPassphrase,   // new passphrase breaks the policy
    EmptyPassphrase,  // unlock with nothing typed
};

const char* vault_error_str(VaultError e);

// Policy for a passphrase that protects a new vault image: 8-128 characters,
// not whitespace-only. Returns Ok or WeakPassphrase.
VaultError check_new_passphrase(const SString& pass);

struct Header {
    uint8_t version = kFormatVersion;
    KdfParams kdf = {0, 0};
    uint8_t salt[kSaltSize] = {};
    uint8_t wrap_nonce[kNonceSize] = {};
    uint8_t wrapped_dek[32] = {};
    uint8_t wrap_mac[kMacSize] = {};

    void encode(uint8_t out[kHeaderSize]) const;
    static VaultError decode(const uint8_t* in, size_t len, Header& out);
};

// Fresh salt + nonce, then wraps `dek` under `kek`. h.kdf must be set first;
// the salt must be generated (header_new_salt) before deriving the KEK.
void header_new_salt(Header& h);
void header_wrap(Header& h, const uint8_t kek[32], const uint8_t dek[32]);
VaultError header_unwrap(const Header& h, const uint8_t kek[32], uint8_t dek[32]);

VaultError seal_file(const Header& h, const uint8_t dek[32], const uint8_t* payload,
                     size_t len, std::vector<uint8_t>& out);
VaultError open_file(const uint8_t* file, size_t len, const uint8_t dek[32], SBytes& payload);

}  // namespace cv

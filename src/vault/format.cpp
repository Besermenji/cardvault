#include "vault/format.h"

#include <cstring>

#include "crypto/rng.h"

namespace cv {

namespace {
const uint8_t kMagic[4] = {'C', 'V', 'L', 'T'};

void put_u32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
}
uint32_t get_u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
}  // namespace

const char* vault_error_str(VaultError e) {
    switch (e) {
        case VaultError::Ok: return "OK";
        case VaultError::Truncated: return "File truncated";
        case VaultError::BadMagic: return "Not a CardVault file";
        case VaultError::UnsupportedVersion: return "Unsupported version";
        case VaultError::BadParams: return "Bad KDF parameters";
        case VaultError::WrongPassphrase: return "Wrong passphrase";
        case VaultError::Corrupt: return "File corrupted";
        case VaultError::TooLarge: return "Vault too large";
        case VaultError::OutOfMemory: return "Out of memory";
        case VaultError::Io: return "Storage error";
        case VaultError::NotOpen: return "Vault locked";
    }
    return "Unknown error";
}

void Header::encode(uint8_t out[kHeaderSize]) const {
    memcpy(out, kMagic, 4);
    out[4] = version;
    out[5] = kKdfArgon2id;
    out[6] = out[7] = 0;
    put_u32(out + 8, kdf.mem_kib);
    put_u32(out + 12, kdf.passes);
    memcpy(out + 16, salt, kSaltSize);
    memcpy(out + 32, wrap_nonce, kNonceSize);
    memcpy(out + 56, wrapped_dek, 32);
    memcpy(out + 88, wrap_mac, kMacSize);
}

VaultError Header::decode(const uint8_t* in, size_t len, Header& h) {
    if (len < kHeaderSize) return VaultError::Truncated;
    if (memcmp(in, kMagic, 4) != 0) return VaultError::BadMagic;
    if (in[4] != kFormatVersion) return VaultError::UnsupportedVersion;
    if (in[5] != kKdfArgon2id) return VaultError::BadParams;
    h.version = in[4];
    h.kdf.mem_kib = get_u32(in + 8);
    h.kdf.passes = get_u32(in + 12);
    if (!kdf_params_valid(h.kdf)) return VaultError::BadParams;
    memcpy(h.salt, in + 16, kSaltSize);
    memcpy(h.wrap_nonce, in + 32, kNonceSize);
    memcpy(h.wrapped_dek, in + 56, 32);
    memcpy(h.wrap_mac, in + 88, kMacSize);
    return VaultError::Ok;
}

void header_new_salt(Header& h) { rng_fill(h.salt, kSaltSize); }

void header_wrap(Header& h, const uint8_t kek[32], const uint8_t dek[32]) {
    rng_fill(h.wrap_nonce, kNonceSize);
    uint8_t enc[kHeaderSize];
    h.encode(enc);  // params part (first 32 bytes) is the AD
    crypto_aead_lock(h.wrapped_dek, h.wrap_mac, kek, h.wrap_nonce, enc, kParamsSize, dek, 32);
}

VaultError header_unwrap(const Header& h, const uint8_t kek[32], uint8_t dek[32]) {
    uint8_t enc[kHeaderSize];
    h.encode(enc);
    if (crypto_aead_unlock(dek, h.wrap_mac, kek, h.wrap_nonce, enc, kParamsSize,
                           h.wrapped_dek, 32) != 0) {
        return VaultError::WrongPassphrase;
    }
    return VaultError::Ok;
}

VaultError seal_file(const Header& h, const uint8_t dek[32], const uint8_t* payload, size_t len,
                     std::vector<uint8_t>& out) {
    if (len > kMaxPayload) return VaultError::TooLarge;
    // Plaintext = u32 length || payload || zero padding to kPadBlock, which
    // hides the exact amount of data stored.
    size_t padded = ((len + 4 + kPadBlock - 1) / kPadBlock) * kPadBlock;
    SBytes pt(padded, 0);
    put_u32(pt.data(), uint32_t(len));
    if (len) memcpy(pt.data() + 4, payload, len);

    out.assign(kHeaderSize + kNonceSize + kMacSize + padded, 0);
    uint8_t* hdr = out.data();
    uint8_t* nonce = hdr + kHeaderSize;
    uint8_t* mac = nonce + kNonceSize;
    uint8_t* ct = mac + kMacSize;
    h.encode(hdr);
    rng_fill(nonce, kNonceSize);
    crypto_aead_lock(ct, mac, dek, nonce, hdr, kHeaderSize, pt.data(), padded);
    return VaultError::Ok;
}

VaultError open_file(const uint8_t* file, size_t len, const uint8_t dek[32], SBytes& payload) {
    Header h;
    VaultError e = Header::decode(file, len, h);
    if (e != VaultError::Ok) return e;
    size_t overhead = kHeaderSize + kNonceSize + kMacSize;
    if (len < overhead + kPadBlock) return VaultError::Truncated;
    size_t ct_len = len - overhead;
    if (ct_len % kPadBlock != 0 || ct_len > kMaxPayload + kPadBlock) return VaultError::Corrupt;

    const uint8_t* nonce = file + kHeaderSize;
    const uint8_t* mac = nonce + kNonceSize;
    const uint8_t* ct = mac + kMacSize;
    SBytes pt(ct_len);
    if (crypto_aead_unlock(pt.data(), mac, dek, nonce, file, kHeaderSize, ct, ct_len) != 0) {
        return VaultError::Corrupt;
    }
    uint32_t n = get_u32(pt.data());
    if (n > ct_len - 4) return VaultError::Corrupt;
    payload.assign(pt.begin() + 4, pt.begin() + 4 + n);
    return VaultError::Ok;
}

}  // namespace cv

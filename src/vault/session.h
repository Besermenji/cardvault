// An unlocked vault: holds the data key and decrypted contents in RAM.
#pragma once

#include "vault/format.h"
#include "vault/model.h"
#include "vault/storage.h"

namespace cv {

class VaultSession {
public:
    VaultSession() = default;
    VaultSession(const VaultSession&) = delete;
    VaultSession& operator=(const VaultSession&) = delete;
    ~VaultSession() { close(); }

    bool is_open() const { return open_; }
    VaultData& data() { return data_; }
    const Header& header() const { return hdr_; }

    // Creates an empty vault protected by `pass` and writes it to `store`.
    VaultError create(BlobStore& store, const SString& pass, const KdfParams& kdf);
    // Reads and decrypts the vault in `store`.
    VaultError unlock(BlobStore& store, const SString& pass);
    // Decrypts a vault image (e.g. an SD backup) and makes it this session.
    VaultError unlock_bytes(const std::vector<uint8_t>& file, const SString& pass);
    // Re-encrypts the current contents (fresh nonce) and writes to `store`.
    VaultError save(BlobStore& store);
    // New salt, new data key, new passphrase; then saves.
    VaultError change_passphrase(BlobStore& store, const SString& new_pass,
                                 const KdfParams& kdf);
    // Encrypts the current contents into a standalone image protected by
    // `pass` (independent salt and data key). Session state is unchanged.
    VaultError export_image(const SString& pass, const KdfParams& kdf,
                            std::vector<uint8_t>& out) const;
    // Forgets keys and wipes all decrypted data.
    void close();

    // Reads only the header (no passphrase needed), e.g. to show KDF cost.
    static VaultError peek_header(const std::vector<uint8_t>& file, Header& out);

private:
    static VaultError build(const VaultData& data, const SString& pass, const KdfParams& kdf,
                            Header& hdr, uint8_t dek[32], std::vector<uint8_t>& out);

    bool open_ = false;
    Header hdr_;
    Key32 dek_;
    VaultData data_;
};

}  // namespace cv

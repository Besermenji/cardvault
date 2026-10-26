#include "vault/session.h"

#include <cstring>

#include "crypto/rng.h"
#include "vault/serializer.h"

namespace cv {

namespace {
VaultError derive(const SString& pass, const Header& h, Key32& kek) {
    if (!kdf_derive(kek.data(), reinterpret_cast<const uint8_t*>(pass.data()), pass.size(),
                    h.salt, h.kdf)) {
        return VaultError::OutOfMemory;
    }
    return VaultError::Ok;
}
}  // namespace

VaultError VaultSession::build(const VaultData& data, const SString& pass, const KdfParams& kdf,
                               Header& hdr, uint8_t dek[32], std::vector<uint8_t>& out) {
    VaultError e = check_new_passphrase(pass);
    if (e != VaultError::Ok) return e;
    if (!kdf_params_valid(kdf)) return VaultError::BadParams;
    hdr = Header();
    hdr.kdf = kdf;
    header_new_salt(hdr);
    Key32 kek;
    e = derive(pass, hdr, kek);
    if (e != VaultError::Ok) return e;
    rng_fill(dek, 32);
    header_wrap(hdr, kek.data(), dek);

    SBytes payload;
    e = serialize(data, payload);
    if (e != VaultError::Ok) return e;
    return seal_file(hdr, dek, payload.data(), payload.size(), out);
}

VaultError VaultSession::create(BlobStore& store, const SString& pass, const KdfParams& kdf) {
    close();
    std::vector<uint8_t> file;
    VaultError e = build(data_, pass, kdf, hdr_, dek_.data(), file);
    if (e != VaultError::Ok) {
        close();
        return e;
    }
    if (!store.write(file)) {
        close();
        return VaultError::Io;
    }
    open_ = true;
    return VaultError::Ok;
}

VaultError VaultSession::unlock(BlobStore& store, const SString& pass) {
    std::vector<uint8_t> file;
    if (!store.read(file)) return VaultError::Io;
    return unlock_bytes(file, pass);
}

VaultError VaultSession::unlock_bytes(const std::vector<uint8_t>& file, const SString& pass) {
    close();
    if (pass.empty()) return VaultError::EmptyPassphrase;
    Header h;
    VaultError e = Header::decode(file.data(), file.size(), h);
    if (e != VaultError::Ok) return e;

    Key32 kek;
    e = derive(pass, h, kek);
    if (e != VaultError::Ok) return e;
    Key32 dek;
    e = header_unwrap(h, kek.data(), dek.data());
    if (e != VaultError::Ok) return e;

    SBytes payload;
    e = open_file(file.data(), file.size(), dek.data(), payload);
    if (e != VaultError::Ok) return e;
    e = deserialize(payload.data(), payload.size(), data_);
    if (e != VaultError::Ok) return e;

    hdr_ = h;
    memcpy(dek_.data(), dek.data(), 32);
    open_ = true;
    return VaultError::Ok;
}

VaultError VaultSession::save(BlobStore& store) {
    if (!open_) return VaultError::NotOpen;
    SBytes payload;
    VaultError e = serialize(data_, payload);
    if (e != VaultError::Ok) return e;
    std::vector<uint8_t> file;
    e = seal_file(hdr_, dek_.data(), payload.data(), payload.size(), file);
    if (e != VaultError::Ok) return e;
    return store.write(file) ? VaultError::Ok : VaultError::Io;
}

VaultError VaultSession::change_passphrase(BlobStore& store, const SString& new_pass,
                                           const KdfParams& kdf) {
    if (!open_) return VaultError::NotOpen;
    Header h;
    Key32 dek;
    std::vector<uint8_t> file;
    VaultError e = build(data_, new_pass, kdf, h, dek.data(), file);
    if (e != VaultError::Ok) return e;
    if (!store.write(file)) return VaultError::Io;
    hdr_ = h;
    memcpy(dek_.data(), dek.data(), 32);
    return VaultError::Ok;
}

VaultError VaultSession::export_image(const SString& pass, const KdfParams& kdf,
                                      std::vector<uint8_t>& out) const {
    if (!open_) return VaultError::NotOpen;
    Header h;
    Key32 dek;
    return build(data_, pass, kdf, h, dek.data(), out);
}

void VaultSession::close() {
    data_.clear();
    wipe(dek_.data(), 32);
    hdr_ = Header();
    open_ = false;
}

VaultError VaultSession::peek_header(const std::vector<uint8_t>& file, Header& out) {
    return Header::decode(file.data(), file.size(), out);
}

}  // namespace cv

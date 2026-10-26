#include "vault/serializer.h"

namespace cv {

namespace {

struct Writer {
    SBytes& out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; i++) u8(uint8_t(v >> (8 * i)));
    }
    bool str(const SString& s) {
        if (s.size() > 0xffff) return false;
        u16(uint16_t(s.size()));
        out.insert(out.end(), s.begin(), s.end());
        return true;
    }
};

struct Reader {
    const uint8_t* p;
    size_t n;
    bool ok = true;
    bool need(size_t k) {
        if (!ok || n < k) return ok = false;
        return true;
    }
    uint8_t u8() {
        if (!need(1)) return 0;
        n--;
        return *p++;
    }
    uint16_t u16() {
        uint16_t lo = u8();
        return uint16_t(lo | (uint16_t(u8()) << 8));
    }
    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= uint32_t(u8()) << (8 * i);
        return v;
    }
    void str(SString& s) {
        uint16_t len = u16();
        if (!need(len)) return;
        s.assign(reinterpret_cast<const char*>(p), len);
        p += len;
        n -= len;
    }
};

bool valid_type(uint8_t t) { return t >= 1 && t <= 4; }

}  // namespace

size_t entry_serialized_size(const Entry& e) {
    // id, type, created, updated, title (len + bytes), field count
    size_t n = 4 + 1 + 4 + 4 + 2 + e.title.size() + 1;
    for (auto& f : e.fields) n += 1 + 2 + f.value.size();  // key, len, bytes
    return n;
}

size_t serialized_size(const VaultData& data) {
    size_t n = 1 + 4 + 2;  // version, next_id, entry count
    for (auto& e : data.entries) n += entry_serialized_size(e);
    return n;
}

VaultError serialize(const VaultData& data, SBytes& out) {
    wipe(out);
    // The limit is judged by serialized_size(), so callers that report how
    // full the vault is use exactly the rule that refuses it here.
    size_t size = serialized_size(data);
    if (size > kMaxPayload) return VaultError::TooLarge;
    out.reserve(size);  // one allocation: no partial copies left by regrowth
    if (data.entries.size() > 0xffff) return VaultError::TooLarge;
    Writer w{out};
    w.u8(kPayloadVersion);
    w.u32(data.next_id);
    w.u16(uint16_t(data.entries.size()));
    for (auto& e : data.entries) {
        if (e.fields.size() > 0xff) return VaultError::TooLarge;
        w.u32(e.id);
        w.u8(uint8_t(e.type));
        w.u32(e.created);
        w.u32(e.updated);
        if (!w.str(e.title)) return VaultError::TooLarge;
        w.u8(uint8_t(e.fields.size()));
        for (auto& f : e.fields) {
            w.u8(f.key);
            if (!w.str(f.value)) return VaultError::TooLarge;
        }
    }
    return out.size() > kMaxPayload ? VaultError::TooLarge : VaultError::Ok;
}

VaultError deserialize(const uint8_t* in, size_t len, VaultData& data) {
    data.clear();
    Reader r{in, len};
    if (r.u8() != kPayloadVersion) return VaultError::UnsupportedVersion;
    data.next_id = r.u32();
    uint16_t count = r.u16();
    for (uint16_t i = 0; i < count && r.ok; i++) {
        // Build entries in place: moving from temporaries would leave short
        // (small-string-optimised) secrets behind on the stack.
        data.entries.emplace_back();
        Entry& e = data.entries.back();
        e.id = r.u32();
        uint8_t type = r.u8();
        if (!valid_type(type)) {
            data.clear();
            return VaultError::Corrupt;
        }
        e.type = EntryType(type);
        e.created = r.u32();
        e.updated = r.u32();
        r.str(e.title);
        uint8_t nf = r.u8();
        for (uint8_t k = 0; k < nf && r.ok; k++) {
            e.fields.emplace_back();
            Field& f = e.fields.back();
            f.key = r.u8();
            r.str(f.value);
        }
    }
    if (!r.ok || r.n != 0) {
        data.clear();
        return VaultError::Corrupt;
    }
    return VaultError::Ok;
}

}  // namespace cv

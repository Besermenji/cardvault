#include "core/update.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "crypto/secure.h"
#include "monocypher-ed25519.h"

namespace cv {

namespace {

uint32_t load_le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// A NUL-padded fixed field: printable ASCII, at least one character, and
// nothing but NULs after the first NUL.
bool fixed_string(const uint8_t* p, size_t n, std::string& out) {
    size_t len = 0;
    while (len < n && p[len]) len++;
    if (len == 0 || len == n) return false;  // empty, or no terminator
    for (size_t i = len; i < n; i++)
        if (p[i]) return false;
    for (size_t i = 0; i < len; i++)
        if (p[i] < 0x21 || p[i] > 0x7e) return false;
    out.assign(reinterpret_cast<const char*>(p), len);
    return true;
}

// One decimal 0..max with no sign, no leading zero (except "0") and at most 3 digits.
bool number(const char*& s, unsigned max, uint8_t& out) {
    if (*s < '0' || *s > '9') return false;
    if (*s == '0' && s[1] >= '0' && s[1] <= '9') return false;
    unsigned v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        if (++digits > 3) return false;
        v = v * 10 + unsigned(*s++ - '0');
    }
    if (v > max) return false;
    out = uint8_t(v);
    return true;
}

}  // namespace

uint32_t Version::packed() const {
    // rc 1..254 sort before the final release (255).
    return uint32_t(major) << 24 | uint32_t(minor) << 16 | uint32_t(patch) << 8 |
           (rc ? rc : 255u);
}

Version Version::unpack(uint32_t v) {
    Version r;
    r.major = uint8_t(v >> 24);
    r.minor = uint8_t(v >> 16);
    r.patch = uint8_t(v >> 8);
    r.rc = uint8_t(v) == 255 ? 0 : uint8_t(v);
    return r;
}

std::string Version::str() const {
    char buf[24];
    if (rc)
        snprintf(buf, sizeof buf, "%u.%u.%u-rc%u", major, minor, patch, rc);
    else
        snprintf(buf, sizeof buf, "%u.%u.%u", major, minor, patch);
    return buf;
}

bool parse_version(const char* s, Version& out) {
    Version v;
    if (!number(s, 255, v.major) || *s++ != '.') return false;
    if (!number(s, 255, v.minor) || *s++ != '.') return false;
    if (!number(s, 255, v.patch)) return false;
    if (*s == '-') {
        if (strncmp(s, "-rc", 3) != 0) return false;
        s += 3;
        if (!number(s, 254, v.rc) || v.rc == 0) return false;
    }
    if (*s) return false;
    out = v;
    return true;
}

int compare_versions(const Version& a, const Version& b) {
    uint32_t x = a.packed(), y = b.packed();
    return x < y ? -1 : x > y ? 1 : 0;
}

UpdateError check_update_header(const uint8_t* buf, size_t len, const uint8_t public_key[32],
                                const Version& running, uint32_t slot_size, UpdateHeader& out) {
    if (len < kUpdateHeaderSize || memcmp(buf, "CVUP", 4) != 0 || buf[4] != kUpdateFormat ||
        buf[5] || buf[6] || buf[7])
        return UpdateError::BadFile;
    if (crypto_ed25519_check(buf + kUpdateSignedSize, public_key, buf, kUpdateSignedSize) != 0)
        return UpdateError::BadSignature;
    // Signed from here on, but still parsed strictly: a signing mistake must
    // not install something unexpected.
    std::string version, target;
    UpdateHeader h;
    if (!fixed_string(buf + 8, 16, version) || !parse_version(version.c_str(), h.version) ||
        !fixed_string(buf + 24, 16, target) || load_le32(buf + 44) != 0)
        return UpdateError::BadFile;
    if (target != kUpdateTarget) return UpdateError::WrongDevice;
    h.image_len = load_le32(buf + 40);
    if (h.image_len == 0) return UpdateError::BadFile;
    if (h.image_len > slot_size) return UpdateError::TooBig;
    memcpy(h.image_hash, buf + 48, 64);
    out = h;  // also for TooOld, so the message can name the version
    return compare_versions(h.version, running) < 0 ? UpdateError::TooOld : UpdateError::Ok;
}

UpdateImageCheck::UpdateImageCheck(const UpdateHeader& h)
    : len_(h.image_len), ctx_(new crypto_sha512_ctx) {
    memcpy(expect_, h.image_hash, sizeof expect_);
    crypto_sha512_init(static_cast<crypto_sha512_ctx*>(ctx_));
}

UpdateImageCheck::~UpdateImageCheck() {
    wipe(ctx_, sizeof(crypto_sha512_ctx));
    delete static_cast<crypto_sha512_ctx*>(ctx_);
}

bool UpdateImageCheck::add(const uint8_t* data, size_t n) {
    if (overflow_ || n > len_ - received_) {
        overflow_ = true;
        return false;
    }
    crypto_sha512_update(static_cast<crypto_sha512_ctx*>(ctx_), data, n);
    received_ += n;
    return true;
}

UpdateError UpdateImageCheck::finish() {
    if (overflow_) return UpdateError::BadFile;
    if (received_ < len_) return UpdateError::Truncated;
    uint8_t got[64];
    crypto_sha512_final(static_cast<crypto_sha512_ctx*>(ctx_), got);
    bool same = crypto_verify64(got, expect_) == 0;
    wipe(got, sizeof got);
    return same ? UpdateError::Ok : UpdateError::HashMismatch;
}

LayoutCheck classify_layout(const std::vector<PartitionInfo>& actual,
                            const std::vector<PartitionInfo>& expected) {
    auto same_set = [](const std::vector<PartitionInfo>& a, const std::vector<PartitionInfo>& b) {
        if (a.size() != b.size()) return false;
        for (const PartitionInfo& p : a)
            if (std::count(b.begin(), b.end(), p) != 1) return false;
        return true;
    };
    if (same_set(actual, expected)) return LayoutCheck::Ok;
    // Before #85 the same table had no app1: one full install fixes that.
    std::vector<PartitionInfo> old;
    for (const PartitionInfo& p : expected)
        if (p.label != "app1") old.push_back(p);
    return same_set(actual, old) ? LayoutCheck::NoSecondSlot : LayoutCheck::NotOwnTable;
}

// Keep in step with partitions.csv / partitions_test.csv (a host test checks).
std::vector<PartitionInfo> release_layout() {
    return {{"nvs", 1, 0x02, 0x9000, 0x5000},      {"otadata", 1, 0x00, 0xe000, 0x2000},
            {"app0", 0, 0x10, 0x10000, 0x300000},  {"vault", 1, 0x82, 0x310000, 0x100000},
            {"app1", 0, 0x11, 0x410000, 0x300000}};
}

std::vector<PartitionInfo> test_layout() {
    return {{"nvs", 1, 0x02, 0x9000, 0x5000},       {"otadata", 1, 0x00, 0xe000, 0x2000},
            {"app0", 0, 0x10, 0x10000, 0x300000},   {"app1", 0, 0x11, 0x410000, 0x300000},
            {"cvtvault", 1, 0x82, 0x710000, 0xF0000}};
}

UpdateOutcome update_outcome(bool pending, uint32_t pending_slot, uint32_t running_slot) {
    if (!pending) return UpdateOutcome::None;
    return pending_slot == running_slot ? UpdateOutcome::Installed : UpdateOutcome::RolledBack;
}

}  // namespace cv

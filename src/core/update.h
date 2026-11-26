// Signed firmware update files (.cvu) for "Update firmware" from the SD card
// (#85). Pure logic, no ESP code, so the native tests cover all of it.
//
// File = header (kUpdateHeaderSize bytes) + the app image (firmware.bin).
// Header, little-endian:
//   0  magic "CVUP"
//   4  format (1), 3 zero bytes
//   8  version, 16 bytes, NUL-padded: "x.y.z" or "x.y.z-rcN"
//  24  target, 16 bytes, NUL-padded: kUpdateTarget
//  40  image length (u32)
//  44  reserved (u32, zero)
//  48  SHA-512 of the image (64 bytes)
// 112  Ed25519 signature over bytes 0..111 (64 bytes)
// tools/make_update.py writes the same layout.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cv {

constexpr size_t kUpdateSignedSize = 112;
constexpr size_t kUpdateHeaderSize = kUpdateSignedSize + 64;
constexpr uint8_t kUpdateFormat = 1;
constexpr const char* kUpdateTarget = "cardputer-adv";

struct Version {
    uint8_t major = 0, minor = 0, patch = 0;
    uint8_t rc = 0;  // 0 = final release; -rcN sorts before the final x.y.z
    // Packed for NVS; the order of packed values is the version order.
    uint32_t packed() const;
    static Version unpack(uint32_t v);
    std::string str() const;
};
// "x.y.z" or "x.y.z-rcN", each number 0..255, rc 1..254. Nothing else.
bool parse_version(const char* s, Version& out);
int compare_versions(const Version& a, const Version& b);  // <0, 0, >0

struct UpdateHeader {
    Version version;
    uint32_t image_len = 0;
    uint8_t image_hash[64] = {};
};

enum class UpdateError {
    Ok,
    BadFile,       // not an update file, or a format this firmware doesn't know
    BadSignature,  // not signed with the key built into this firmware
    WrongDevice,   // signed, but for another target
    TooBig,        // image larger than the free slot
    TooOld,        // older than the running version (equal is allowed)
    Truncated,     // the file ends before the image does
    HashMismatch,  // image bytes differ from the signed hash
};

// Checks a header in the order that never trusts unsigned fields: layout,
// signature, then target, size and version. `running` is CV_VERSION parsed.
// `out` is filled on Ok and on TooOld (the header is genuine, only older).
UpdateError check_update_header(const uint8_t* buf, size_t len, const uint8_t public_key[32],
                                const Version& running, uint32_t slot_size, UpdateHeader& out);

// Feeds the image bytes after a checked header; finish() says whether they
// were exactly the signed image.
class UpdateImageCheck {
public:
    explicit UpdateImageCheck(const UpdateHeader& h);
    ~UpdateImageCheck();
    UpdateImageCheck(const UpdateImageCheck&) = delete;
    UpdateImageCheck& operator=(const UpdateImageCheck&) = delete;
    // False once more bytes arrive than the header announced.
    bool add(const uint8_t* data, size_t n);
    size_t received() const { return received_; }
    UpdateError finish();

private:
    uint8_t expect_[64];
    uint32_t len_;
    size_t received_ = 0;
    bool overflow_ = false;
    void* ctx_;  // crypto_sha512_ctx
};

// The flash layout this firmware updates on. An update is only offered when
// the running partition table is exactly CardVault's own: otherwise "the
// other slot" may hold Launcher or other firmware.
struct PartitionInfo {
    std::string label;
    uint8_t type = 0;  // 0 app, 1 data (ESP-IDF numbering)
    uint8_t subtype = 0;
    uint32_t address = 0, size = 0;
    bool operator==(const PartitionInfo& o) const {
        return label == o.label && type == o.type && subtype == o.subtype &&
               address == o.address && size == o.size;
    }
};
enum class LayoutCheck {
    Ok,
    NoSecondSlot,  // CardVault's table from before #85 (v0.3.0 and older)
    NotOwnTable,   // Launcher or anything else
};
// `actual` in any order; `expected` is the table this firmware was built with.
LayoutCheck classify_layout(const std::vector<PartitionInfo>& actual,
                            const std::vector<PartitionInfo>& expected);
// The partition table this firmware was built with (partitions.csv, or
// partitions_test.csv in test builds). A host test compares both with the CSVs.
std::vector<PartitionInfo> release_layout();
#if !defined(ARDUINO) || defined(CV_UPDATE_TEST_KEY)
std::vector<PartitionInfo> test_layout();  // test builds and host tests only
#endif

// What the boot after an install found: the slot the update was written to
// is running (and confirmed), or the bootloader marked it invalid and went
// back. Anything else (e.g. a USB install since) says nothing.
enum class UpdateOutcome { None, Installed, RolledBack };
UpdateOutcome update_outcome(bool pending, uint32_t pending_slot, uint32_t running_slot,
                             bool pending_slot_invalid);

}  // namespace cv

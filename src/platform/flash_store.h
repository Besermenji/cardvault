// Vault blob on the internal flash ("vault" LittleFS partition).
// begin() returns false when the partition is missing or not mountable;
// has_partition() tells the two apart.
#pragma once

#include <cstdint>

#include "vault/storage.h"

namespace cv {

// Vault address in partitions_test.csv (production: 0x310000, partitions.csv).
constexpr uint32_t kTestVaultAddress = 0x410000;

class FlashStore : public BlobStore {
public:
    // Mounts the partition. A blank (fully erased) partition is formatted
    // automatically; anything else that fails to mount is left untouched so
    // a damaged filesystem is never silently destroyed.
    bool begin();
    // Erases and formats the partition (explicit user action).
    bool format() { return erase() && mounted_; }
    bool exists() override;
    bool read(std::vector<uint8_t>& out) override;
    bool write(const std::vector<uint8_t>& data) override;
    // Erases the entire partition (all blocks), then tries to remount it.
    // The result says only whether the data was destroyed; check mounted()
    // for the remount.
    bool erase() override;
    bool mounted() const { return mounted_; }
    static bool has_partition();
    // Flash address of the "vault" partition, 0 if there is none.
    static uint32_t partition_address();
    // True when the vault sits where partitions_test.csv puts it. Test and
    // debug builds (hooks, screenshots) refuse to run anywhere else, so they
    // can never reach a real vault.
    static bool on_test_partition() { return partition_address() == kTestVaultAddress; }

#ifdef CV_TEST_HOOKS
    // Test builds only (e2e `storage` hook): which vault files exist, and how
    // many times the vault magic is found anywhere in the raw partition, which
    // also counts old copies left behind by copy-on-write.
    struct Probe {
        bool mounted = false;
        bool vault = false;
        bool tmp = false;
        bool bak = false;
        bool scanned = false;  // false: the partition could not be read
        uint32_t magic_hits = 0;
    };
    Probe probe();
#endif

private:
    bool mounted_ = false;
    const char* label_ = nullptr;
};

}  // namespace cv

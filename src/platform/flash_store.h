// Vault blob on the internal flash ("vault" LittleFS partition).
// begin() returns false when the partition is missing or not mountable;
// has_partition() tells the two apart.
#pragma once

#include "vault/storage.h"

namespace cv {

class FlashStore : public BlobStore {
public:
    // Mounts the partition. A blank (fully erased) partition is formatted
    // automatically; anything else that fails to mount is left untouched so
    // a damaged filesystem is never silently destroyed.
    bool begin();
    // Erases and formats the partition (explicit user action).
    bool format() { return erase(); }
    bool exists() override;
    bool read(std::vector<uint8_t>& out) override;
    bool write(const std::vector<uint8_t>& data) override;
    // Erases the entire partition (all blocks), then reformats it.
    bool erase() override;
    bool mounted() const { return mounted_; }
    static bool has_partition();

private:
    bool mounted_ = false;
    const char* label_ = nullptr;
};

}  // namespace cv

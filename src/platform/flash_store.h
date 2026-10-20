// Vault blob on the internal flash ("vault" LittleFS partition).
#pragma once

#include "vault/storage.h"

namespace cv {

class FlashStore : public BlobStore {
public:
    bool begin();
    bool exists() override;
    bool read(std::vector<uint8_t>& out) override;
    bool write(const std::vector<uint8_t>& data) override;
    // Erases the entire partition (all blocks), then reformats it.
    bool erase() override;
    bool mounted() const { return mounted_; }

private:
    bool mounted_ = false;
    const char* label_ = nullptr;
};

}  // namespace cv

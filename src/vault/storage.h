// Abstract persistent storage for one vault blob (flash, SD card, RAM in tests).
#pragma once

#include <cstdint>
#include <vector>

namespace cv {

class BlobStore {
public:
    virtual ~BlobStore() = default;
    virtual bool exists() = 0;
    virtual bool read(std::vector<uint8_t>& out) = 0;
    // Must be atomic: after a crash either the old or the new blob is present.
    virtual bool write(const std::vector<uint8_t>& data) = 0;
    virtual bool erase() = 0;
};

class MemoryStore : public BlobStore {
public:
    bool present = false;
    std::vector<uint8_t> blob;
    bool exists() override { return present; }
    bool read(std::vector<uint8_t>& out) override {
        if (!present) return false;
        out = blob;
        return true;
    }
    bool write(const std::vector<uint8_t>& data) override {
        blob = data;
        present = true;
        return true;
    }
    bool erase() override {
        blob.clear();
        present = false;
        return true;
    }
};

}  // namespace cv

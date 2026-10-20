// Tiny persistent key/value interface (NVS on device, map in tests).
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace cv {

class KvStore {
public:
    virtual ~KvStore() = default;
    virtual bool get_u32(const char* key, uint32_t& out) = 0;
    // Must be durable (committed) when it returns true.
    virtual bool set_u32(const char* key, uint32_t value) = 0;
};

class MemoryKv : public KvStore {
public:
    std::map<std::string, uint32_t> values;
    bool fail_writes = false;
    bool get_u32(const char* key, uint32_t& out) override {
        auto it = values.find(key);
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
    bool set_u32(const char* key, uint32_t value) override {
        if (fail_writes) return false;
        values[key] = value;
        return true;
    }
};

}  // namespace cv

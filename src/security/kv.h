// Tiny persistent key/value interface (NVS on device, map in tests).
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace cv {

enum class KvRead {
    Ok,
    Missing,  // key definitely not stored
    Error,    // storage failed: the value is unknown, not zero
};

class KvStore {
public:
    virtual ~KvStore() = default;
    virtual KvRead get_u32(const char* key, uint32_t& out) = 0;
    // Must be durable (committed) when it returns true.
    virtual bool set_u32(const char* key, uint32_t value) = 0;
};

class MemoryKv : public KvStore {
public:
    std::map<std::string, uint32_t> values;
    bool fail_reads = false;
    bool fail_writes = false;
    KvRead get_u32(const char* key, uint32_t& out) override {
        if (fail_reads) return KvRead::Error;
        auto it = values.find(key);
        if (it == values.end()) return KvRead::Missing;
        out = it->second;
        return KvRead::Ok;
    }
    bool set_u32(const char* key, uint32_t value) override {
        if (fail_writes) return false;
        values[key] = value;
        return true;
    }
};

}  // namespace cv

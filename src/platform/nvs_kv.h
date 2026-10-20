// KvStore backed by NVS (Preferences commits on every write).
#pragma once

#include <Preferences.h>

#include "security/kv.h"

namespace cv {

class NvsKv : public KvStore {
public:
    explicit NvsKv(const char* ns) : ns_(ns) {}
    bool begin() { return prefs_.begin(ns_, false); }
    bool get_u32(const char* key, uint32_t& out) override {
        if (!prefs_.isKey(key)) return false;
        out = prefs_.getUInt(key, 0);
        return true;
    }
    bool set_u32(const char* key, uint32_t value) override {
        return prefs_.putUInt(key, value) == sizeof(uint32_t);
    }

private:
    const char* ns_;
    Preferences prefs_;
};

}  // namespace cv

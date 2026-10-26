// KvStore backed by NVS. Uses the ESP-IDF API directly (not Preferences),
// so a read error is never mistaken for a stored 0.
#pragma once

#include <nvs.h>

#include "security/kv.h"

namespace cv {

class NvsKv : public KvStore {
public:
    explicit NvsKv(const char* ns) : ns_(ns) {}
    ~NvsKv() override {
        if (open_) nvs_close(h_);
    }
    // Read-only does not create the namespace, so it fails if it never existed.
    bool begin(bool read_only = false) {
        open_ = nvs_open(ns_, read_only ? NVS_READONLY : NVS_READWRITE, &h_) == ESP_OK;
        return open_;
    }
    KvRead get_u32(const char* key, uint32_t& out) override {
        if (!open_) return KvRead::Error;
        esp_err_t e = nvs_get_u32(h_, key, &out);
        if (e == ESP_OK) return KvRead::Ok;
        return e == ESP_ERR_NVS_NOT_FOUND ? KvRead::Missing : KvRead::Error;
    }
    bool set_u32(const char* key, uint32_t value) override {
        return open_ && nvs_set_u32(h_, key, value) == ESP_OK && nvs_commit(h_) == ESP_OK;
    }

private:
    const char* ns_;
    nvs_handle_t h_ = 0;
    bool open_ = false;
};

}  // namespace cv

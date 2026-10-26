#include "platform/flash_store.h"

#include <LittleFS.h>
#include <esp_partition.h>

#include "vault/format.h"

namespace cv {

namespace {
const char* kFile = "/vault.bin";
const char* kTmp = "/vault.tmp";  // next version, verified before it replaces kFile
const char* kBak = "/vault.bak";  // previous version, only while a rename fallback runs

const esp_partition_t* find_partition(const char*& label) {
    // Only ever our own "vault" partition. When installed through Launcher,
    // Launcher creates it from the label in our partition table; we never fall
    // back to shared partitions (spiffs, littlefs, fat) that other firmware
    // may format or overwrite.
    label = "vault";
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
}

bool read_file(const char* path, std::vector<uint8_t>& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    // Cap before allocating: a damaged or foreign file must not exhaust the heap.
    size_t size = f.size();
    if (size == 0 || size > kMaxFileSize) {
        f.close();
        return false;
    }
    out.resize(size);
    size_t n = f.read(out.data(), out.size());
    f.close();
    return n == out.size();
}

bool valid_image(const char* path, std::vector<uint8_t>& out) {
    return LittleFS.exists(path) && read_file(path, out) &&
           check_layout(out.data(), out.size()) == VaultError::Ok;
}

// Writes `data` to `path` and reads it back.
bool write_verified(const char* path, const std::vector<uint8_t>& data) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t n = f.write(data.data(), data.size());
    f.flush();
    f.close();
    std::vector<uint8_t> check;
    return n == data.size() && read_file(path, check) && check == data;
}

// After a crash during write(): vault.bin may be missing while a complete
// image sits in vault.tmp (newer) or vault.bak (older). Promote the newest
// valid one; leftovers next to a vault are stale ciphertext and are deleted.
// Returns false when a valid image exists but cannot be promoted: it is then
// kept and the caller must not offer to set up a new vault over it.
bool recover_leftovers() {
    if (!LittleFS.exists(kFile)) {
        for (const char* path : {kTmp, kBak}) {
            std::vector<uint8_t> img;
            if (!valid_image(path, img)) continue;
            if (LittleFS.rename(path, kFile)) break;
            // Rename refused: copy instead (there is no vault.bin to protect).
            if (write_verified(kFile, img)) break;
            LittleFS.remove(kFile);
            return false;  // keep `path`: it may be the only copy
        }
    }
    if (LittleFS.exists(kFile)) {
        LittleFS.remove(kTmp);
        LittleFS.remove(kBak);
    } else {
        std::vector<uint8_t> img;
        for (const char* path : {kTmp, kBak})
            if (LittleFS.exists(path) && !valid_image(path, img)) LittleFS.remove(path);
    }
    return true;
}

bool partition_blank(const esp_partition_t* p) {
    uint8_t buf[256];
    for (size_t off = 0; off < 16384 && off < p->size; off += sizeof buf) {
        if (esp_partition_read(p, off, buf, sizeof buf) != ESP_OK) return false;
        for (uint8_t b : buf)
            if (b != 0xFF) return false;
    }
    return true;
}
}  // namespace

bool FlashStore::has_partition() {
    const char* label = nullptr;
    return find_partition(label) != nullptr;
}

bool FlashStore::begin() {
    const esp_partition_t* p = find_partition(label_);
    if (!p) return false;
    mounted_ = LittleFS.begin(false, "/vault", 4, label_);
    if (!mounted_ && partition_blank(p)) mounted_ = LittleFS.begin(true, "/vault", 4, label_);
    // A vault that cannot be promoted reads as "storage unreadable", which
    // asks before formatting, instead of "no vault", which would set up a new
    // one over it.
    if (mounted_ && !recover_leftovers()) {
        LittleFS.end();
        mounted_ = false;
    }
    return mounted_;
}

bool FlashStore::exists() { return mounted_ && LittleFS.exists(kFile); }

bool FlashStore::read(std::vector<uint8_t>& out) { return mounted_ && read_file(kFile, out); }

bool FlashStore::write(const std::vector<uint8_t>& data) {
    if (!mounted_) return false;
    // Verify what landed on flash before replacing the good copy.
    if (!write_verified(kTmp, data)) {
        LittleFS.remove(kTmp);
        return false;
    }
    // LittleFS renames atomically over an existing file.
    if (LittleFS.rename(kTmp, kFile)) return true;
    // Fallback for VFS layers that refuse to rename over an existing file:
    // move the good copy aside, never delete it first. begin() recovers from
    // a crash at any step (tmp is newer than bak, both are complete).
    if (LittleFS.exists(kFile)) {
        LittleFS.remove(kBak);
        if (LittleFS.rename(kFile, kBak)) {
            if (LittleFS.rename(kTmp, kFile)) {
                LittleFS.remove(kBak);
                return true;
            }
            LittleFS.rename(kBak, kFile);  // put the good copy back
        }
    }
    LittleFS.remove(kTmp);
    return false;
}

bool FlashStore::erase() {
    const char* label = nullptr;
    const esp_partition_t* p = find_partition(label);
    if (!p) return false;
    if (mounted_) LittleFS.end();
    mounted_ = false;
    bool ok = esp_partition_erase_range(p, 0, p->size) == ESP_OK;
    // Remount result is reported separately through mounted().
    mounted_ = LittleFS.begin(true, "/vault", 4, label);
    return ok;
}

}  // namespace cv

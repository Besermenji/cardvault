#include "platform/flash_store.h"

#include <LittleFS.h>
#include <esp_partition.h>

namespace cv {

namespace {
const char* kFile = "/vault.bin";
const char* kTmp = "/vault.tmp";

const esp_partition_t* find_partition(const char*& label) {
    // Our own partition table has "vault"; when installed through Launcher the
    // app runs with Launcher's table, which provides "spiffs".
    for (const char* l : {"vault", "spiffs"}) {
        auto* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, l);
        if (p) {
            label = l;
            return p;
        }
    }
    return nullptr;
}

bool read_file(const char* path, std::vector<uint8_t>& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    out.resize(f.size());
    size_t n = f.read(out.data(), out.size());
    f.close();
    return n == out.size();
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

bool FlashStore::begin() {
    const esp_partition_t* p = find_partition(label_);
    if (!p) return false;
    mounted_ = LittleFS.begin(false, "/vault", 4, label_);
    if (!mounted_ && partition_blank(p)) mounted_ = LittleFS.begin(true, "/vault", 4, label_);
    if (mounted_ && !LittleFS.exists(kFile) && LittleFS.exists(kTmp)) {
        // Crash between writing the temp file and renaming it: finish the job.
        LittleFS.rename(kTmp, kFile);
    }
    return mounted_;
}

bool FlashStore::exists() { return mounted_ && LittleFS.exists(kFile); }

bool FlashStore::read(std::vector<uint8_t>& out) { return mounted_ && read_file(kFile, out); }

bool FlashStore::write(const std::vector<uint8_t>& data) {
    if (!mounted_) return false;
    File f = LittleFS.open(kTmp, "w");
    if (!f) return false;
    size_t n = f.write(data.data(), data.size());
    f.flush();
    f.close();
    if (n != data.size()) {
        LittleFS.remove(kTmp);
        return false;
    }
    // Verify what landed on flash before replacing the good copy.
    std::vector<uint8_t> check;
    if (!read_file(kTmp, check) || check != data) {
        LittleFS.remove(kTmp);
        return false;
    }
    if (LittleFS.rename(kTmp, kFile)) return true;
    // Fallback for VFS layers that refuse to rename over an existing file.
    LittleFS.remove(kFile);
    return LittleFS.rename(kTmp, kFile);
}

bool FlashStore::erase() {
    const char* label = nullptr;
    const esp_partition_t* p = find_partition(label);
    if (!p) return false;
    if (mounted_) LittleFS.end();
    mounted_ = false;
    bool ok = esp_partition_erase_range(p, 0, p->size) == ESP_OK;
    mounted_ = LittleFS.begin(true, "/vault", 4, label);
    return ok && mounted_;
}

}  // namespace cv

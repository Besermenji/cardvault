#include "platform/sd_backup.h"

#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#include "core/backup_names.h"
#include "vault/format.h"

namespace cv {
namespace sd {

namespace {
constexpr int kSck = 40, kMiso = 39, kMosi = 14, kCs = 12;
constexpr uint32_t kSpiHz = 25000000;
constexpr uint32_t kMountTimeoutMs = 3000;
const char* kDir = "/cardvault";
bool g_mounted = false;
bool g_wedged = false;

// SD.begin runs in its own task so a stalled card cannot freeze the UI. The
// state is static: a task that never returns must not touch freed memory.
struct MountJob {
    volatile bool done;
    volatile bool ok;
};
MountJob g_job;

void mount_task(void*) {
    // Same wiring as M5's Cardputer SD example: the default SPI bus.
    SPI.begin(kSck, kMiso, kMosi, kCs);
    g_job.ok = SD.begin(kCs, SPI, kSpiHz);
    g_job.done = true;
    vTaskDelete(nullptr);
}
}  // namespace

bool wedged() { return g_wedged; }

bool mount() {
    if (g_mounted) return true;
    if (g_wedged) return false;
    g_job.done = false;
    g_job.ok = false;
    if (xTaskCreatePinnedToCore(mount_task, "sd_mount", 6144, nullptr, 1, nullptr, 0) != pdPASS)
        return false;
    for (uint32_t waited = 0; !g_job.done; waited += 10) {
        if (waited >= kMountTimeoutMs) {
            g_wedged = true;
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    g_mounted = g_job.ok;
    if (!g_mounted) {
        SPI.end();
        return false;
    }
    if (!SD.exists(kDir)) SD.mkdir(kDir);
    return true;
}

void unmount() {
    if (!g_mounted) return;
    SD.end();
    SPI.end();
    g_mounted = false;
}

std::vector<std::string> list_backups() {
    std::vector<std::string> names;
    if (!mount()) return names;
    File dir = SD.open(kDir);
    if (!dir) return names;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        std::string n = f.name();
        auto slash = n.rfind('/');
        if (slash != std::string::npos) n = n.substr(slash + 1);
        if (!f.isDirectory() && n.size() > 4 && n.substr(n.size() - 4) == ".cvb") names.push_back(n);
        f.close();
    }
    dir.close();
    sort_backups(names);
    return names;
}

bool write_backup(const std::vector<uint8_t>& image, std::string& name) {
    if (image.empty() || image.size() > kMaxFileSize) return false;
    if (!mount()) return false;
    long number = next_backup_number(list_backups());
    if (number < 0) return false;
    // Never reuse a name that exists (even one list_backups did not show): a
    // backup must not overwrite or extend an older one.
    std::string path;
    for (int tries = 0;; tries++, number++) {
        if (tries == 100) return false;
        name = backup_name(number);
        path = std::string(kDir) + "/" + name;
        if (!SD.exists(path.c_str())) break;
    }
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return false;
    size_t n = f.write(image.data(), image.size());
    f.close();
    std::vector<uint8_t> check;
    if (n == image.size() && read_backup(name, check) && check == image) return true;
    SD.remove(path.c_str());  // never leave a partial or unverified backup behind
    return false;
}

bool read_backup(const std::string& name, std::vector<uint8_t>& out) {
    if (!mount()) return false;
    std::string path = std::string(kDir) + "/" + name;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    // Cap before allocating: a huge file must not exhaust the heap.
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

bool remove_backup(const std::string& name) {
    if (!mount()) return false;
    return SD.remove((std::string(kDir) + "/" + name).c_str());
}

}  // namespace sd
}  // namespace cv

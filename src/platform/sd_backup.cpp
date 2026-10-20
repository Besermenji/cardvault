#include "platform/sd_backup.h"

#include <SD.h>
#include <SPI.h>

#include <algorithm>

namespace cv {
namespace sd {

namespace {
constexpr int kSck = 40, kMiso = 39, kMosi = 14, kCs = 12;
const char* kDir = "/cardvault";
bool g_mounted = false;
SPIClass g_spi(HSPI);
}  // namespace

bool mount() {
    if (g_mounted) return true;
    g_spi.begin(kSck, kMiso, kMosi, kCs);
    g_mounted = SD.begin(kCs, g_spi, 25000000);
    if (g_mounted && !SD.exists(kDir)) SD.mkdir(kDir);
    return g_mounted;
}

void unmount() {
    if (!g_mounted) return;
    SD.end();
    g_spi.end();
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
    std::sort(names.rbegin(), names.rend());
    return names;
}

bool write_backup(const std::vector<uint8_t>& image, std::string& name) {
    if (!mount()) return false;
    int next = 1;
    for (auto& n : list_backups()) {
        int v = atoi(n.c_str() + 7);  // "backup-0001.cvb"
        if (n.rfind("backup-", 0) == 0 && v >= next) next = v + 1;
    }
    char buf[32];
    snprintf(buf, sizeof buf, "backup-%04d.cvb", next);
    name = buf;
    std::string path = std::string(kDir) + "/" + name;
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return false;
    size_t n = f.write(image.data(), image.size());
    f.close();
    if (n != image.size()) return false;
    std::vector<uint8_t> check;
    return read_backup(name, check) && check == image;
}

bool read_backup(const std::string& name, std::vector<uint8_t>& out) {
    if (!mount()) return false;
    std::string path = std::string(kDir) + "/" + name;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    out.resize(f.size());
    size_t n = f.read(out.data(), out.size());
    f.close();
    return n == out.size();
}

}  // namespace sd
}  // namespace cv

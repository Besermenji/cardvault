// Encrypted vault backups on the microSD card (/cardvault/*.cvb).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cv {

namespace sd {

// Mounts the card. Never blocks longer than kMountTimeoutMs: if the driver
// stalls (seen with some cards, #43) it returns false and wedged() is true;
// later calls then fail at once until the device restarts.
bool mount();
bool wedged();
void unmount();
// Writes a new numbered backup file; returns its name in `name`.
bool write_backup(const std::vector<uint8_t>& image, std::string& name);
std::vector<std::string> list_backups();  // newest first
bool read_backup(const std::string& name, std::vector<uint8_t>& out);
bool remove_backup(const std::string& name);

}  // namespace sd

}  // namespace cv

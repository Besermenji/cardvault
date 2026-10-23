// Encrypted vault backups on the microSD card (/cardvault/*.cvb).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cv {

namespace sd {

bool mount();
void unmount();
// Writes a new numbered backup file; returns its name in `name`.
bool write_backup(const std::vector<uint8_t>& image, std::string& name);
std::vector<std::string> list_backups();  // newest first
bool read_backup(const std::string& name, std::vector<uint8_t>& out);
bool remove_backup(const std::string& name);

}  // namespace sd

}  // namespace cv

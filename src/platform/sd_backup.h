// Encrypted vault backups on the microSD card (/cardvault/*.cvb).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cv {

namespace sd {

// Mounts the card. Never blocks the UI longer than a few seconds: if the
// driver stalls (#43) it returns false and wedged() is true. A stalled mount
// that finishes later cleans up after itself; until then mount() fails at
// once, afterwards it may be tried again.
bool mount();
bool wedged();
void unmount();

// Keeps the card mounted while held (captured by a prompt's callback) and
// unmounts when the last copy is destroyed: on success, cancel or lock.
struct MountHold {
    MountHold() = default;
    MountHold(const MountHold&) = delete;
    MountHold& operator=(const MountHold&) = delete;
    ~MountHold() { unmount(); }
};
// Writes a new numbered backup file; returns its name in `name`.
bool write_backup(const std::vector<uint8_t>& image, std::string& name);
std::vector<std::string> list_backups();  // newest first
bool read_backup(const std::string& name, std::vector<uint8_t>& out);
bool remove_backup(const std::string& name);

}  // namespace sd

}  // namespace cv

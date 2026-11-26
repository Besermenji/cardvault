// Encrypted vault backups on the microSD card (/cardvault/*.cvb).
#pragma once

#include <cstdint>
#include <functional>
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

// Firmware update files (/cardvault/*.cvu, #85), sorted by name.
std::vector<std::string> list_updates();
// Reads up to `n` bytes from the start of an update file into `buf`; `got`
// is how many there were, `size` the whole file's size.
bool read_update_head(const std::string& name, uint8_t* buf, size_t n, size_t& got, size_t& size);
// Reads the file from `offset` to its end in chunks of up to `chunk` bytes.
// `sink` returns false to stop. False if the card failed or sink stopped.
bool stream_update(const std::string& name, size_t offset, size_t chunk,
                   const std::function<bool(const uint8_t*, size_t)>& sink);

#ifdef CV_TEST_HOOKS
// Test builds only: simulates pulling the card by holding the SPI bus's MISO
// input high, as a missing card does (every read returns 0xFF). The real
// SD driver and FAT code then fail on their own. The cut also holds over a
// remount (mount() applies it again), until test_cut_miso(false).
void test_cut_miso(bool cut);
// Arms a cut `ms` milliseconds after the next backup write or read, or update
// stream, is entered
// (the mount time counts), so it lands in the middle of an operation that
// blocks the main loop. -1 disarms.
void test_cut_after_ms(int32_t ms);
#endif

}  // namespace sd

}  // namespace cv

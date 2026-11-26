// Firmware slots (app0/app1) and the bootloader's rollback state (#85).
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/update.h"

namespace cv {

namespace ota {

// Marks the running image as good. Until this runs, an image installed from
// the SD card is on probation: if the device resets first, the bootloader
// starts the previous version again. Called once the vault storage has
// opened (unlock/setup), and before the restarts of the storage set-up and
// the boot-time wipe. Under Launcher, Launcher is the "previous version".
void confirm_boot();

// Label of the running app partition ("app0", "app1", or Launcher's names).
const char* running_slot();

// Whether this device runs on CardVault's own partition table (the one this
// firmware was built with), so the other slot is free for an update.
LayoutCheck layout();

// Size of the slot an update would be written to (0 if there is none).
uint32_t free_slot_size();

// Writes an image to the slot that is not running. Nothing changes what boots
// until finish() succeeds; abort() (or the destructor) drops a partial write.
class Installer {
public:
    Installer() = default;
    ~Installer() { abort(); }
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;
    bool begin(size_t image_len);
    bool write(const uint8_t* data, size_t n);
    // Lets ESP-IDF validate the image, then makes it the boot slot and records
    // `version` so the next boot can say what happened.
    bool finish(const Version& version);
    void abort();

private:
    uint32_t handle_ = 0;
    const void* part_ = nullptr;  // esp_partition_t
    bool open_ = false;
};

// After an install and restart: was the new version started and confirmed,
// or did the bootloader go back? Clears the record (None after the first call).
UpdateOutcome take_outcome(Version& installed);

#ifdef CV_TEST_HOOKS
// Test builds only: the next freshly installed image restarts without
// confirming itself, as a broken image would.
void test_skip_confirm_once();
#endif

}  // namespace ota

}  // namespace cv

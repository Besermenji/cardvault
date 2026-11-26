// Firmware slots (app0/app1) and the bootloader's rollback state (#85).
#pragma once

namespace cv {

namespace ota {

// Marks the running image as good. Until this runs, an image installed from
// the SD card is on probation: if the device resets first, the bootloader
// starts the previous version again. Call it once the vault storage has
// opened, so an image that can't read the vault rolls back.
void confirm_boot();

// Label of the running app partition ("app0", "app1", or Launcher's names).
const char* running_slot();

}  // namespace ota

}  // namespace cv

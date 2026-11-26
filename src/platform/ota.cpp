#include "platform/ota.h"

#include <esp_ota_ops.h>

// The Arduino core marks a freshly installed image valid before setup() runs
// unless this returns true (esp32-hal-misc.c, initArduino). CardVault decides
// itself, in confirm_boot().
extern "C" bool verifyRollbackLater() { return true; }

namespace cv {

namespace ota {

void confirm_boot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
        esp_ota_mark_app_valid_cancel_rollback();
}

const char* running_slot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running ? running->label : "?";
}

}  // namespace ota

}  // namespace cv

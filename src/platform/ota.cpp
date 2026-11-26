#include "platform/ota.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

#include <cstring>
#include <vector>

#include "platform/device.h"       // CV_NVS_CFG
#include "platform/flash_store.h"  // CV_VAULT_LABEL
#include "platform/nvs_kv.h"

// The Arduino core marks a freshly installed image valid before setup() runs
// unless this returns true (esp32-hal-misc.c, initArduino). CardVault decides
// itself, in confirm_boot().
extern "C" bool verifyRollbackLater() { return true; }

namespace cv {

namespace ota {

namespace {

// In the settings namespace: the version that was just installed, and the
// slot it went to plus one (0 = nothing pending).
constexpr const char* kKeyVersion = "upd_ver";
constexpr const char* kKeySlot = "upd_slot";
#ifdef CV_TEST_HOOKS
constexpr const char* kKeySkipConfirm = "t_noconfirm";
#endif

uint32_t slot_index(const esp_partition_t* p) {
    return p ? uint32_t(p->subtype) - ESP_PARTITION_SUBTYPE_APP_OTA_MIN : 0xFF;
}

}  // namespace

void confirm_boot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY)
        return;
#ifdef CV_TEST_HOOKS
    NvsKv kv(CV_NVS_CFG);
    uint32_t skip = 0;
    if (kv.begin() && kv.get_u32(kKeySkipConfirm, skip) == KvRead::Ok && skip) {
        kv.set_u32(kKeySkipConfirm, 0);
        esp_restart();  // still pending: the bootloader goes back to the old slot
    }
#endif
    esp_ota_mark_app_valid_cancel_rollback();
}

const char* running_slot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running ? running->label : "?";
}

LayoutCheck layout() {
    std::vector<PartitionInfo> actual;
    for (esp_partition_type_t type : {ESP_PARTITION_TYPE_APP, ESP_PARTITION_TYPE_DATA}) {
        esp_partition_iterator_t it = esp_partition_find(type, ESP_PARTITION_SUBTYPE_ANY, nullptr);
        for (; it; it = esp_partition_next(it)) {
            const esp_partition_t* p = esp_partition_get(it);
            actual.push_back({p->label, uint8_t(p->type), uint8_t(p->subtype), p->address, p->size});
        }
        esp_partition_iterator_release(it);
    }
    // Test builds use partitions_test.csv, recognised by their vault label.
    bool test_table = strcmp(CV_VAULT_LABEL, "vault") != 0;
    return classify_layout(actual, test_table ? test_layout() : release_layout());
}

uint32_t free_slot_size() {
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    return next ? next->size : 0;
}

bool Installer::begin(size_t image_len) {
    abort();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    if (!next || next == esp_ota_get_running_partition()) return false;
    esp_ota_handle_t h = 0;
    // The image size erases only what is needed (OTA_SIZE_UNKNOWN erases the whole slot).
    if (esp_ota_begin(next, image_len, &h) != ESP_OK) return false;
    handle_ = h;
    part_ = next;
    open_ = true;
    return true;
}

bool Installer::write(const uint8_t* data, size_t n) {
    return open_ && esp_ota_write(handle_, data, n) == ESP_OK;
}

bool Installer::finish(const Version& version) {
    if (!open_) return false;
    open_ = false;
    // esp_ota_end checks the image (segments, checksum, appended SHA-256).
    if (esp_ota_end(handle_) != ESP_OK) return false;
    const esp_partition_t* next = static_cast<const esp_partition_t*>(part_);
    if (esp_ota_set_boot_partition(next) != ESP_OK) return false;
    // Only now: a record without a switched slot would announce a rollback.
    NvsKv kv(CV_NVS_CFG);
    if (kv.begin()) {
        kv.set_u32(kKeyVersion, version.packed());
        kv.set_u32(kKeySlot, slot_index(next) + 1);
    }
    return true;
}

void Installer::abort() {
    if (open_) esp_ota_abort(handle_);
    open_ = false;
}

UpdateOutcome take_outcome(Version& installed) {
    NvsKv kv(CV_NVS_CFG);
    uint32_t slot = 0, version = 0;
    if (!kv.begin(true) || kv.get_u32(kKeySlot, slot) != KvRead::Ok || slot == 0 ||
        kv.get_u32(kKeyVersion, version) != KvRead::Ok)
        return UpdateOutcome::None;
    NvsKv rw(CV_NVS_CFG);
    if (rw.begin()) rw.set_u32(kKeySlot, 0);
    installed = Version::unpack(version);
    return update_outcome(true, slot - 1, slot_index(esp_ota_get_running_partition()));
}

#ifdef CV_TEST_HOOKS
void test_skip_confirm_once() {
    NvsKv kv(CV_NVS_CFG);
    if (kv.begin()) kv.set_u32(kKeySkipConfirm, 1);
}
#endif

}  // namespace ota

}  // namespace cv

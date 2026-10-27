#include "platform/sd_backup.h"

#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <mutex>

#ifdef CV_TEST_HOOKS
#include <esp_rom_gpio.h>
#include <esp_timer.h>
#include <soc/gpio_pins.h>
#include <soc/gpio_sig_map.h>
#endif

#include "core/backup_names.h"
#include "vault/format.h"

namespace cv {
namespace sd {

namespace {
constexpr int kSck = 40, kMiso = 39, kMosi = 14, kCs = 12;
constexpr uint32_t kSpiHz = 25000000;
constexpr uint32_t kMountTimeoutMs = 5000;
const char* kDir = "/cardvault";
bool g_mounted = false;

// SD.begin runs in its own task so a stalled card cannot freeze the UI.
// State is static and atomic: the task may outlive the call that started it.
enum JobState : int { kIdle, kRunning, kDone, kAbandoned };
std::atomic<int> g_job{kIdle};
std::atomic<bool> g_job_ok{false};

#ifdef CV_TEST_HOOKS
// MISO is held high while g_cut is set. SPI.begin() attaches the pin again, so
// mount_task() applies the cut after it: a card pulled stays pulled over a remount.
bool g_cut = false;
std::atomic<int32_t> g_cut_after_ms{-1};
esp_timer_handle_t g_cut_timer = nullptr;
// Serialises the timer callback with the hook commands: esp_timer_stop() does
// not wait for a callback that is already running.
std::mutex g_cut_mutex;
bool g_cut_pending = false;  // a timer is armed and has not fired

void apply_cut() {
    esp_rom_gpio_connect_in_signal(g_cut ? GPIO_MATRIX_CONST_ONE_INPUT : uint32_t(kMiso),
                                   FSPIQ_IN_IDX, false);
}

void cut_timer_cb(void*) {
    std::lock_guard<std::mutex> lock(g_cut_mutex);
    if (!g_cut_pending) return;
    g_cut_pending = false;
    g_cut = true;
    apply_cut();
}

// Called as a backup write or read is entered, before anything can return early,
// so an armed cut is always used by the very next operation.
void arm_cut() {
    int32_t ms = g_cut_after_ms.exchange(-1);
    if (ms < 0) return;
    std::lock_guard<std::mutex> lock(g_cut_mutex);
    if (ms == 0) {
        g_cut = true;
        apply_cut();
        return;
    }
    if (!g_cut_timer) {
        esp_timer_create_args_t args = {};
        args.callback = cut_timer_cb;
        args.name = "sd_cut";
        if (esp_timer_create(&args, &g_cut_timer) != ESP_OK) return;
    }
    esp_timer_stop(g_cut_timer);
    g_cut_pending = true;
    esp_timer_start_once(g_cut_timer, uint64_t(ms) * 1000);
}
#else
inline void arm_cut() {}
#endif

void mount_task(void*) {
    // Same wiring as M5's Cardputer SD example: the default SPI bus.
    SPI.begin(kSck, kMiso, kMosi, kCs);
#ifdef CV_TEST_HOOKS
    {
        std::lock_guard<std::mutex> lock(g_cut_mutex);
        if (g_cut) apply_cut();
    }
#endif
    bool ok = SD.begin(kCs, SPI, kSpiHz);
    g_job_ok = ok;
    int expected = kRunning;
    if (!g_job.compare_exchange_strong(expected, kDone)) {
        // The caller gave up waiting: release the card and bus ourselves.
        if (ok) SD.end();
        SPI.end();
        g_job = kIdle;
    }
    vTaskDelete(nullptr);
}

}  // namespace

#ifdef CV_TEST_HOOKS
void test_cut_miso(bool cut) {
    std::lock_guard<std::mutex> lock(g_cut_mutex);
    if (g_cut_timer) esp_timer_stop(g_cut_timer);
    g_cut_pending = false;
    g_cut = cut;
    apply_cut();
}
void test_cut_after_ms(int32_t ms) {
    std::lock_guard<std::mutex> lock(g_cut_mutex);
    if (g_cut_timer) esp_timer_stop(g_cut_timer);
    g_cut_pending = false;
    g_cut_after_ms = ms;
}
#endif

bool wedged() { return g_job == kAbandoned; }

bool mount() {
    if (g_mounted) return true;
    int idle = kIdle;
    if (!g_job.compare_exchange_strong(idle, kRunning)) return false;  // stalled mount pending
    g_job_ok = false;
    if (xTaskCreatePinnedToCore(mount_task, "sd_mount", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
        g_job = kIdle;
        return false;
    }
    for (uint32_t waited = 0; g_job == kRunning; waited += 10) {
        if (waited >= kMountTimeoutMs) {
            int running = kRunning;
            if (g_job.compare_exchange_strong(running, kAbandoned)) return false;
            break;  // finished just now
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    g_job = kIdle;
    g_mounted = g_job_ok;
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
    arm_cut();
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
        if (backup_number(name) != number) return false;  // past the numbering range
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
    arm_cut();
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

// On-device integration tests: run on a real Cardputer ADV with
//   pio test -e cardputer-test   (suite: test_device_platform)
// They use the test partition table (vault at 0x410000) and test NVS
// namespaces, so a real vault on the device is never touched.
#include <Arduino.h>
#include <LittleFS.h>
#include <M5Cardputer.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <unity.h>

#include "core/flows.h"
#include "crypto/rng.h"
#include "platform/device.h"
#include "platform/flash_store.h"
#include "platform/nvs_kv.h"
#include "platform/sd_backup.h"
#include "security/attempt_guard.h"
#include "vault/session.h"

using namespace cv;

static const esp_partition_t* vault_part() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "vault");
}

static void erase_vault_partition() {
    LittleFS.end();
    const esp_partition_t* p = vault_part();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(ESP_OK, esp_partition_erase_range(p, 0, p->size));
}

void setUp() {}
void tearDown() {}

// ---- Safety: make sure we are on the test layout ------------------------------

void test_runs_on_test_partition_layout() {
    const esp_partition_t* p = vault_part();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x410000, p->address,
                                    "refusing to run: not the test partition table");
}

// ---- FlashStore -------------------------------------------------------------

void test_blank_partition_is_formatted_and_mounted() {
    erase_vault_partition();
    FlashStore s;
    TEST_ASSERT_TRUE(FlashStore::has_partition());
    TEST_ASSERT_TRUE(s.begin());
    TEST_ASSERT_FALSE(s.exists());
}

void test_unreadable_partition_is_never_formatted() {
    erase_vault_partition();
    const esp_partition_t* p = vault_part();
    uint8_t junk[64];
    for (int i = 0; i < 64; i++) junk[i] = uint8_t(i * 37 + 11);
    TEST_ASSERT_EQUAL(ESP_OK, esp_partition_write(p, 0, junk, sizeof junk));
    FlashStore s;
    TEST_ASSERT_FALSE(s.begin());
    uint8_t back[64];
    esp_partition_read(p, 0, back, sizeof back);
    TEST_ASSERT_EQUAL_MEMORY(junk, back, sizeof junk);  // untouched
    TEST_ASSERT_TRUE(s.format());
    TEST_ASSERT_TRUE(s.mounted());
}

void test_write_read_overwrite_roundtrip() {
    erase_vault_partition();
    FlashStore s;
    TEST_ASSERT_TRUE(s.begin());
    std::vector<uint8_t> a(50000), b(1234), out;
    rng_fill(a.data(), a.size());
    rng_fill(b.data(), b.size());
    TEST_ASSERT_TRUE(s.write(a));
    TEST_ASSERT_TRUE(s.exists());
    TEST_ASSERT_TRUE(s.read(out));
    TEST_ASSERT_TRUE(out == a);
    TEST_ASSERT_TRUE(s.write(b));
    TEST_ASSERT_TRUE(s.read(out));
    TEST_ASSERT_TRUE(out == b);
    TEST_ASSERT_FALSE(LittleFS.exists("/vault.tmp"));
}

void test_persists_across_remount() {
    erase_vault_partition();
    std::vector<uint8_t> data(4096, 0x5A), out;
    {
        FlashStore s;
        TEST_ASSERT_TRUE(s.begin());
        TEST_ASSERT_TRUE(s.write(data));
    }
    LittleFS.end();
    FlashStore again;
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_TRUE(again.read(out));
    TEST_ASSERT_TRUE(out == data);
}

void test_crash_before_rename_is_recovered() {
    erase_vault_partition();
    FlashStore s;
    TEST_ASSERT_TRUE(s.begin());
    // Simulate: new file fully written to vault.tmp, power lost before the
    // rename, and no previous vault.bin.
    File f = LittleFS.open("/vault.tmp", "w");
    f.write(reinterpret_cast<const uint8_t*>("NEWDATA"), 7);
    f.close();
    LittleFS.end();
    FlashStore after;
    TEST_ASSERT_TRUE(after.begin());
    TEST_ASSERT_TRUE(after.exists());
    std::vector<uint8_t> out;
    after.read(out);
    TEST_ASSERT_EQUAL(7, out.size());
    TEST_ASSERT_EQUAL_MEMORY("NEWDATA", out.data(), 7);
}

void test_stale_tmp_does_not_replace_good_vault() {
    erase_vault_partition();
    FlashStore s;
    TEST_ASSERT_TRUE(s.begin());
    std::vector<uint8_t> good(100, 1), out;
    TEST_ASSERT_TRUE(s.write(good));
    // Crash while writing the next version: a partial tmp file remains.
    File f = LittleFS.open("/vault.tmp", "w");
    f.write(reinterpret_cast<const uint8_t*>("PART"), 4);
    f.close();
    LittleFS.end();
    FlashStore after;
    TEST_ASSERT_TRUE(after.begin());
    TEST_ASSERT_TRUE(after.read(out));
    TEST_ASSERT_TRUE(out == good);
    // The next save still works and replaces the stale temp file.
    std::vector<uint8_t> next(200, 2);
    TEST_ASSERT_TRUE(after.write(next));
    TEST_ASSERT_TRUE(after.read(out));
    TEST_ASSERT_TRUE(out == next);
}

void test_erase_destroys_everything() {
    FlashStore s;
    TEST_ASSERT_TRUE(s.begin());
    std::vector<uint8_t> data(3000, 0x77);
    TEST_ASSERT_TRUE(s.write(data));
    TEST_ASSERT_TRUE(s.erase());
    TEST_ASSERT_FALSE(s.exists());
    // No trace of the old bytes anywhere in the partition.
    const esp_partition_t* p = vault_part();
    std::vector<uint8_t> buf(4096);
    size_t hits = 0;
    for (size_t off = 0; off < p->size; off += buf.size()) {
        esp_partition_read(p, off, buf.data(), buf.size());
        for (size_t i = 0; i + 16 <= buf.size(); i += 16) {
            bool run = true;
            for (int k = 0; k < 16; k++) run &= buf[i + k] == 0x77;
            hits += run;
        }
    }
    TEST_ASSERT_EQUAL(0, hits);
}

// ---- NVS / attempt guard -------------------------------------------------------

void test_nvs_guard_persists_across_instances() {
    {
        NvsKv kv(CV_NVS_GUARD);
        TEST_ASSERT_TRUE(kv.begin());
        AttemptGuard g(kv);
        g.reset();
        TEST_ASSERT_TRUE(g.begin_attempt());
        TEST_ASSERT_TRUE(g.begin_attempt());
    }
    NvsKv kv2(CV_NVS_GUARD);
    TEST_ASSERT_TRUE(kv2.begin());
    AttemptGuard g2(kv2);
    TEST_ASSERT_EQUAL(2, g2.failures());
    g2.reset();
    TEST_ASSERT_EQUAL(0, g2.failures());
}

void test_test_build_uses_isolated_nvs() {
    TEST_ASSERT_EQUAL_STRING("cvt_guard", CV_NVS_GUARD);
    TEST_ASSERT_EQUAL_STRING("cvt_cfg", CV_NVS_CFG);
}

// ---- KDF on real hardware ------------------------------------------------------

void test_kdf_calibration_fits_heap_and_time() {
    // Allocate what the UI holds during an unlock (screen canvas) first.
    void* canvas = heap_caps_malloc(240 * 135 * 2, MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(canvas);
    KdfParams p = device::calibrate_kdf(2000);
    char msg[160];
    snprintf(msg, sizeof msg, "calibrated mem=%u KiB passes=%u largest_free=%u",
             unsigned(p.mem_kib), unsigned(p.passes),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE(kdf_params_valid(p));
    TEST_ASSERT_TRUE_MESSAGE(p.mem_kib >= 128, "Argon2 memory below 128 KiB");

    uint8_t out[32], salt[16] = {1};
    uint32_t t0 = millis();
    TEST_ASSERT_TRUE(kdf_derive(out, reinterpret_cast<const uint8_t*>("pass"), 4, salt, p));
    uint32_t ms = millis() - t0;
    snprintf(msg, sizeof msg, "unlock KDF took %u ms", unsigned(ms));
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(ms >= 1000 && ms <= 4000, "unlock time outside 1-4 s");
    heap_caps_free(canvas);
}

void test_full_vault_cycle_on_flash() {
    erase_vault_partition();
    FlashStore store;
    TEST_ASSERT_TRUE(store.begin());
    NvsKv kv(CV_NVS_GUARD);
    kv.begin();
    AttemptGuard guard(kv);
    guard.reset();
    KdfParams p = device::calibrate_kdf(500);
    VaultSession s;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.create(store, "device test pass", p)));
    Entry e;
    e.type = EntryType::Mnemonic;
    e.title = "test seed";
    e.set(kFieldWords, "abandon abandon abandon abandon abandon abandon abandon abandon "
                       "abandon abandon abandon about");
    s.data().add(e);
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.save(store)));
    s.close();
    UnlockResult bad = unlock_attempt(guard, s, store, "wrong");
    TEST_ASSERT_EQUAL(int(UnlockResult::Wrong), int(bad.kind));
    TEST_ASSERT_EQUAL(1, guard.failures());
    UnlockResult ok = unlock_attempt(guard, s, store, "device test pass");
    TEST_ASSERT_EQUAL(int(UnlockResult::Unlocked), int(ok.kind));
    TEST_ASSERT_EQUAL_STRING("test seed", s.data().entries[0].title.c_str());
    TEST_ASSERT_EQUAL(0, guard.failures());
    // Raw flash contains no plaintext.
    const esp_partition_t* part = vault_part();
    std::vector<uint8_t> buf(4096);
    bool found = false;
    for (size_t off = 0; off < part->size && !found; off += buf.size()) {
        esp_partition_read(part, off, buf.data(), buf.size());
        std::string chunk(buf.begin(), buf.end());
        found = chunk.find("abandon") != std::string::npos || chunk.find("test seed") != std::string::npos;
    }
    TEST_ASSERT_FALSE_MESSAGE(found, "plaintext found in raw flash");
}

// ---- RNG ----------------------------------------------------------------------

void test_hw_rng_output_looks_random() {
    const size_t n = 8192;
    std::vector<uint8_t> buf(n);
    rng_hw_fill(buf.data(), n);
    uint32_t counts[256] = {0};
    for (uint8_t b : buf) counts[b]++;
    double chi = 0, expected = double(n) / 256;
    for (uint32_t c : counts) chi += (c - expected) * (c - expected) / expected;
    char msg[64];
    snprintf(msg, sizeof msg, "chi-square(255 dof) = %.1f", chi);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(chi > 150 && chi < 380, "byte distribution looks non-random");
    std::vector<uint8_t> again(64);
    rng_fill(again.data(), 64);
    std::vector<uint8_t> again2(64);
    rng_fill(again2.data(), 64);
    TEST_ASSERT_TRUE(again != again2);
}

void test_battery_reading_survives_rng_use() {
    int before = device::battery_percent();
    uint8_t tmp[256];
    for (int i = 0; i < 20; i++) rng_fill(tmp, sizeof tmp);
    int after = device::battery_percent();
    char msg[64];
    snprintf(msg, sizeof msg, "battery before=%d after=%d", before, after);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE(after >= 0 && after <= 100);
    TEST_ASSERT_TRUE_MESSAGE(abs(after - before) <= 10, "battery reading disturbed by RNG");
}

// ---- SD card (skipped without a working card) --------------------------------

void test_sd_backup_roundtrip() {
    if (!sd::mount()) TEST_IGNORE_MESSAGE("no working SD card inserted");
    std::vector<uint8_t> image(5000), back;
    rng_fill(image.data(), image.size());
    std::string name;
    TEST_ASSERT_TRUE(sd::write_backup(image, name));
    TEST_ASSERT_TRUE(sd::read_backup(name, back));
    TEST_ASSERT_TRUE(back == image);
    auto list = sd::list_backups();
    TEST_ASSERT_TRUE(!list.empty() && list[0] == name);  // newest first
    TEST_ASSERT_TRUE(sd::remove_backup(name));
    sd::unmount();
}

void setup() {
    delay(2500);  // let the USB serial port come up
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, false);
    UNITY_BEGIN();
    RUN_TEST(test_runs_on_test_partition_layout);
    if (vault_part() && vault_part()->address == 0x410000) {
        RUN_TEST(test_blank_partition_is_formatted_and_mounted);
        RUN_TEST(test_unreadable_partition_is_never_formatted);
        RUN_TEST(test_write_read_overwrite_roundtrip);
        RUN_TEST(test_persists_across_remount);
        RUN_TEST(test_crash_before_rename_is_recovered);
        RUN_TEST(test_stale_tmp_does_not_replace_good_vault);
        RUN_TEST(test_erase_destroys_everything);
        RUN_TEST(test_nvs_guard_persists_across_instances);
        RUN_TEST(test_test_build_uses_isolated_nvs);
        RUN_TEST(test_kdf_calibration_fits_heap_and_time);
        RUN_TEST(test_full_vault_cycle_on_flash);
        RUN_TEST(test_hw_rng_output_looks_random);
        RUN_TEST(test_battery_reading_survives_rng_use);
        RUN_TEST(test_sd_backup_roundtrip);
        erase_vault_partition();
    }
    UNITY_END();
}

// Idle after the run; USB stays connected so the device can be reflashed.
void loop() { delay(1000); }

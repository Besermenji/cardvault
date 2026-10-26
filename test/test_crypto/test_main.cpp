#include <unity.h>

#include <cstring>

#include "../common/test_util.h"
#include "crypto/kdf.h"
#include "crypto/kdf_calibrate.h"
#include "crypto/rng.h"
#include "crypto/secure.h"
#include "crypto/sha.h"

using namespace cv;

void setUp() {}
void tearDown() {}

static std::string sha1_hex(const char* s) {
    uint8_t h[20];
    sha1(reinterpret_cast<const uint8_t*>(s), strlen(s), h);
    return to_hex(h, 20);
}
static std::string sha256_hex(const char* s) {
    uint8_t h[32];
    sha256(reinterpret_cast<const uint8_t*>(s), strlen(s), h);
    return to_hex(h, 32);
}

void test_sha1_vectors() {
    TEST_ASSERT_EQUAL_STRING("da39a3ee5e6b4b0d3255bfef95601890afd80709", sha1_hex("").c_str());
    TEST_ASSERT_EQUAL_STRING("a9993e364706816aba3e25717850c26c9cd0d89d", sha1_hex("abc").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
        sha1_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
}

void test_sha256_vectors() {
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                             sha256_hex("").c_str());
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                             sha256_hex("abc").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
}

void test_hmac_sha1_rfc2202() {
    uint8_t mac[20];
    std::vector<uint8_t> key(20, 0x0b);
    const char* msg = "Hi There";
    hmac_sha1(key.data(), key.size(), reinterpret_cast<const uint8_t*>(msg), strlen(msg), mac);
    TEST_ASSERT_EQUAL_STRING("b617318655057264e28bc0b6fb378c8ef146be00", to_hex(mac, 20).c_str());

    std::vector<uint8_t> big(80, 0xaa);
    const char* msg2 = "Test Using Larger Than Block-Size Key - Hash Key First";
    hmac_sha1(big.data(), big.size(), reinterpret_cast<const uint8_t*>(msg2), strlen(msg2), mac);
    TEST_ASSERT_EQUAL_STRING("aa4ae5e15272d00e95705637ce8a3b55ed402112", to_hex(mac, 20).c_str());
}

void test_kdf_deterministic_and_salted() {
    KdfParams p{64, 3};
    uint8_t salt1[16] = {1}, salt2[16] = {2};
    uint8_t a[32], b[32], c[32], d[32];
    const uint8_t* pw = reinterpret_cast<const uint8_t*>("correct horse");
    TEST_ASSERT_TRUE(kdf_derive(a, pw, 13, salt1, p));
    TEST_ASSERT_TRUE(kdf_derive(b, pw, 13, salt1, p));
    TEST_ASSERT_TRUE(kdf_derive(c, pw, 13, salt2, p));
    TEST_ASSERT_TRUE(kdf_derive(d, pw, 12, salt1, p));
    TEST_ASSERT_EQUAL_MEMORY(a, b, 32);
    TEST_ASSERT_TRUE(memcmp(a, c, 32) != 0);
    TEST_ASSERT_TRUE(memcmp(a, d, 32) != 0);
}

void test_kdf_rejects_bad_params() {
    uint8_t out[32], salt[16] = {};
    TEST_ASSERT_FALSE(kdf_derive(out, nullptr, 0, salt, KdfParams{8, 3}));
    TEST_ASSERT_FALSE(kdf_derive(out, nullptr, 0, salt, KdfParams{64, 1}));
    TEST_ASSERT_FALSE(kdf_derive(out, nullptr, 0, salt, KdfParams{65, 3}));
}

void test_kdf_work_cap_edges() {
    // Exactly at the cap is valid; one step over is not.
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{96, 1000}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{104, 1000}));
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{256, 390}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{256, 391}));
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{4096, 24}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{4096, 25}));
    // Calibrated rc1-style params stay valid.
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{256, 200}));
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{128, 56}));  // measured on the ADV
    // 32-bit overflow of mem * passes must not slip under the cap.
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{4096, 0xFFFFFFFFu}));
    TEST_ASSERT_EQUAL(1000, kdf_max_passes(64));
    TEST_ASSERT_EQUAL(781, kdf_max_passes(128));
    TEST_ASSERT_EQUAL(390, kdf_max_passes(256));
    TEST_ASSERT_EQUAL(24, kdf_max_passes(4096));
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{4096, kdf_max_passes(4096)}));
}

// ---- KDF calibration against a synthetic timing model ----------------------------

namespace {
struct Model {
    double overhead_us = 20000;         // malloc + wipe, independent of passes
    double us_per_kib_pass = 95;        // Cardputer ADV measured ~0.095 ms
    double cold_extra_us = 0;           // added to the first call only
    double check_slowdown = 1.0;        // applied from the 4th call on (the closed-loop check)
    int calls = 0;
    uint32_t operator()(const KdfParams& p) {
        calls++;
        double t = overhead_us + double(p.passes) * p.mem_kib * us_per_kib_pass;
        if (calls == 1) t += cold_extra_us;
        if (calls >= 4) t *= check_slowdown;
        return uint32_t(t);
    }
};
KdfTuning tune(Model& m, uint32_t mem = 128, uint32_t target_ms = 2000) {
    return kdf_tune(mem, target_ms, [&m](const KdfParams& p) { return m(p); });
}
double model_ms(const Model& m, const KdfParams& p) {
    return (m.overhead_us + double(p.passes) * p.mem_kib * m.us_per_kib_pass) / 1000.0;
}
}  // namespace

void test_tune_hits_target_and_estimates_overhead() {
    Model m;
    m.overhead_us = 150000;  // large fixed cost the old extrapolation counted as per-pass
    KdfTuning t = tune(m);
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::Ok), int(t.status));
    TEST_ASSERT_FALSE(t.retuned);
    TEST_ASSERT_EQUAL(4, m.calls);  // warm-up, A, B, check
    TEST_ASSERT_TRUE(kdf_params_valid(t.params));
    TEST_ASSERT_FLOAT_WITHIN(60.0, 2000.0, model_ms(m, t.params));
    TEST_ASSERT_UINT32_WITHIN(5000, 150000, t.overhead_us);
    TEST_ASSERT_UINT32_WITHIN(200, 128 * 95, t.per_pass_us);
}

void test_tune_ignores_cold_start() {
    Model m;
    m.cold_extra_us = 900000;  // first derive is far slower than the rest
    KdfTuning t = tune(m);
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::Ok), int(t.status));
    TEST_ASSERT_FALSE(t.retuned);
    TEST_ASSERT_FLOAT_WITHIN(60.0, 2000.0, model_ms(m, t.params));
}

void test_tune_retunes_once_when_check_is_off() {
    Model m;
    m.check_slowdown = 2.0;  // the real derive turns out twice as slow as the probes
    KdfTuning t = tune(m);
    TEST_ASSERT_TRUE(t.retuned);
    TEST_ASSERT_EQUAL(4, m.calls);  // one retune, not re-checked
    m.check_slowdown = 1.0;
    double ms = model_ms(m, t.params) * 2.0;  // what the slow device really takes
    TEST_ASSERT_FLOAT_WITHIN(250.0, 2000.0, ms);
    TEST_ASSERT_TRUE(t.params.passes < 100);
}

void test_tune_reports_slow_and_fast_clamps() {
    Model slow;
    slow.us_per_kib_pass = 20000;  // 128 KiB x 3 passes already takes ~7.7 s
    KdfTuning s = tune(slow);
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::RaisedToMin), int(s.status));
    TEST_ASSERT_EQUAL(kMinKdfPasses, s.params.passes);

    Model fast;
    fast.us_per_kib_pass = 0.05;
    fast.overhead_us = 100;
    KdfTuning f = tune(fast);
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::CappedAtMax), int(f.status));
    TEST_ASSERT_EQUAL(kdf_max_passes(128), f.params.passes);
    TEST_ASSERT_TRUE(kdf_params_valid(f.params));
}

void test_tune_fails_when_a_derive_cannot_run() {
    for (int fail_at = 1; fail_at <= 4; fail_at++) {
        int calls = 0;
        KdfTuning t = kdf_tune(128, 2000, [&](const KdfParams& p) {
            return ++calls == fail_at ? 0u : uint32_t(p.passes * 12000 + 20000);
        });
        TEST_ASSERT_EQUAL(int(KdfTuneStatus::Failed), int(t.status));
    }
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::Failed),
                      int(kdf_tune(7, 2000, [](const KdfParams&) { return 1000u; }).status));
}

void test_tune_survives_noisy_probes() {
    // Probe B no slower than A: no slope to fit, still valid parameters.
    int calls = 0;
    KdfTuning t = kdf_tune(128, 2000, [&](const KdfParams& p) {
        calls++;
        return calls <= 2 ? 100000u : (calls == 3 ? 90000u : uint32_t(p.passes * 12000));
    });
    TEST_ASSERT_TRUE(t.status != KdfTuneStatus::Failed);
    TEST_ASSERT_TRUE(kdf_params_valid(t.params));
}

void test_pick_mem_follows_heap() {
    TEST_ASSERT_EQUAL(224, kdf_pick_mem(320 * 1024));
    TEST_ASSERT_EQUAL(128, kdf_pick_mem(250 * 1024));
    TEST_ASSERT_EQUAL(128, kdf_pick_mem(160 * 1024));
    TEST_ASSERT_EQUAL(64, kdf_pick_mem(150 * 1024));
    TEST_ASSERT_EQUAL(64, kdf_pick_mem(0));
    TEST_ASSERT_EQUAL(4096, kdf_pick_mem(size_t(64) * 1024 * 1024));
    for (size_t kib = 0; kib < 9000; kib += 7) {
        uint32_t mem = kdf_pick_mem(kib * 1024);
        TEST_ASSERT_EQUAL(0, mem % 8);
        TEST_ASSERT_TRUE(mem >= kMinKdfMemKib && mem <= kMaxKdfMemKib);
        if (mem > kMinKdfMemKib) TEST_ASSERT_TRUE(mem * 1024 + 32 * 1024 <= kib * 1024);
    }
}

void test_rng_uniform_range_and_variety() {
    int seen[10] = {0};
    for (int i = 0; i < 2000; i++) {
        uint32_t v = rng_uniform(10);
        TEST_ASSERT_TRUE(v < 10);
        seen[v]++;
    }
    for (int i = 0; i < 10; i++) TEST_ASSERT_TRUE(seen[i] > 100);

    uint8_t a[64], b[64];
    rng_fill(a, sizeof a);
    rng_fill(b, sizeof b);
    TEST_ASSERT_TRUE(memcmp(a, b, 64) != 0);
}

void test_wiping_containers() {
    SString s("secret value that is long enough to live on the heap");
    wipe(s);
    TEST_ASSERT_TRUE(s.empty());
    SBytes b(32, 0xAB);
    wipe(b);
    TEST_ASSERT_TRUE(b.empty());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sha1_vectors);
    RUN_TEST(test_sha256_vectors);
    RUN_TEST(test_hmac_sha1_rfc2202);
    RUN_TEST(test_kdf_deterministic_and_salted);
    RUN_TEST(test_kdf_rejects_bad_params);
    RUN_TEST(test_kdf_work_cap_edges);
    RUN_TEST(test_tune_hits_target_and_estimates_overhead);
    RUN_TEST(test_tune_ignores_cold_start);
    RUN_TEST(test_tune_retunes_once_when_check_is_off);
    RUN_TEST(test_tune_reports_slow_and_fast_clamps);
    RUN_TEST(test_tune_fails_when_a_derive_cannot_run);
    RUN_TEST(test_tune_survives_noisy_probes);
    RUN_TEST(test_pick_mem_follows_heap);
    RUN_TEST(test_rng_uniform_range_and_variety);
    RUN_TEST(test_wiping_containers);
    return UNITY_END();
}

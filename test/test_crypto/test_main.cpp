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

// RFC 9106 section 5.3 (Argon2id): the vendored Monocypher against the spec,
// including lanes, secret and associated data, which our wrapper never uses.
void test_argon2id_rfc9106_vector() {
    uint8_t pass[32], salt[16], key[8], ad[12], tag[32];
    memset(pass, 0x01, sizeof pass);
    memset(salt, 0x02, sizeof salt);
    memset(key, 0x03, sizeof key);
    memset(ad, 0x04, sizeof ad);
    static uint8_t work[32 * 1024];
    crypto_argon2_config cfg = {CRYPTO_ARGON2_ID, 32, 3, 4};
    crypto_argon2_inputs in = {pass, salt, sizeof pass, sizeof salt};
    crypto_argon2_extras ex = {key, ad, sizeof key, sizeof ad};
    crypto_argon2(tag, sizeof tag, work, cfg, in, ex);
    TEST_ASSERT_EQUAL_STRING("0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659",
                             to_hex(tag, sizeof tag).c_str());
}

// Through our wrapper (1 lane, no secret/AD, 16-byte salt, 32-byte output).
// Reference value from the argon2-cffi package (libargon2), not from this code:
//   hash_secret_raw(b"correct horse battery", bytes(range(1, 17)), time_cost=3,
//                   memory_cost=64, parallelism=1, hash_len=32, type=Type.ID,
//                   version=19)
void test_kdf_derive_known_answer() {
    uint8_t salt[16];
    for (int i = 0; i < 16; i++) salt[i] = uint8_t(i + 1);
    uint8_t out[32];
    const char* pw = "correct horse battery";
    TEST_ASSERT_TRUE(kdf_derive(out, reinterpret_cast<const uint8_t*>(pw), strlen(pw), salt,
                                KdfParams{64, 3}));
    TEST_ASSERT_EQUAL_STRING("9342d618db7d56d10b88a161b58228bf82879fa6ebfcd45286937ac5dcd4b124",
                             to_hex(out, sizeof out).c_str());
}

void test_kdf_rejects_bad_params() {
    uint8_t out[32], salt[16] = {};
    TEST_ASSERT_FALSE(kdf_derive(out, nullptr, 0, salt, KdfParams{8, 3}));
    TEST_ASSERT_FALSE(kdf_derive(out, nullptr, 0, salt, KdfParams{64, 1}));
    TEST_ASSERT_FALSE(kdf_derive(out, nullptr, 0, salt, KdfParams{65, 3}));
}

// Each per-field cap is tested where only that cap can reject (the work cap
// and the multiple-of-8 rule stay out of the way).
void test_kdf_field_cap_edges() {
    // Memory: 4096 is the most allowed; 4104 is rejected by the cap alone.
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{kMaxKdfMemKib, kMinKdfPasses}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{kMaxKdfMemKib + 8, kMinKdfPasses}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{0xFFFFFFF8u, kMinKdfPasses}));
    // Memory must be a multiple of 8: 4097 and 4100 fail even though near the cap.
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{kMaxKdfMemKib + 1, kMinKdfPasses}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{4100, kMinKdfPasses}));
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{kMinKdfMemKib, kMinKdfPasses}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{kMinKdfMemKib - 8, kMinKdfPasses}));
    // Passes: 1000 at minimum memory is the most allowed (work 64000 < cap).
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{kMinKdfMemKib, kMaxKdfPasses}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{kMinKdfMemKib, kMaxKdfPasses + 1}));
    TEST_ASSERT_TRUE(kdf_params_valid(KdfParams{kMinKdfMemKib, kMinKdfPasses}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{kMinKdfMemKib, kMinKdfPasses - 1}));
    TEST_ASSERT_FALSE(kdf_params_valid(KdfParams{kMinKdfMemKib, 0}));
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
    double overhead_us = 20000;   // malloc + wipe, independent of passes
    double us_per_kib_pass = 95;  // Cardputer ADV measured ~0.095 ms
    double cold_extra_us = 0;     // added to the first call only
    int calls = 0;
    double total_us = 0;          // time spent in all calls
    uint32_t operator()(const KdfParams& p) {
        calls++;
        double t = overhead_us + double(p.passes) * p.mem_kib * us_per_kib_pass;
        if (calls == 1) t += cold_extra_us;
        total_us += t;
        return uint32_t(t);
    }
    KdfTimer timer() {
        return [this](const KdfParams& p) { return (*this)(p); };
    }
    double ms(const KdfParams& p) const {
        return (overhead_us + double(p.passes) * p.mem_kib * us_per_kib_pass) / 1000.0;
    }
};
}  // namespace

void test_tune_hits_target_and_estimates_overhead() {
    Model m;
    m.overhead_us = 150000;  // large fixed cost the old extrapolation counted as per-pass
    KdfTuning t = kdf_tune(128, 2000, m.timer());
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::Ok), int(t.status));
    TEST_ASSERT_EQUAL(3, m.calls);  // warm-up, A, B: no extra derive at the target
    TEST_ASSERT_TRUE(m.total_us < 1000000);
    TEST_ASSERT_TRUE(kdf_params_valid(t.params));
    TEST_ASSERT_FLOAT_WITHIN(60.0, 2000.0, m.ms(t.params));
    TEST_ASSERT_UINT32_WITHIN(5000, 150000, t.overhead_us);
    TEST_ASSERT_UINT32_WITHIN(200, 128 * 95, t.per_pass_us);
    TEST_ASSERT_UINT32_WITHIN(60, 2000, kdf_estimate_ms(t));
}

void test_tune_ignores_cold_start() {
    Model m;
    m.cold_extra_us = 900000;  // first derive is far slower than the rest
    KdfTuning t = kdf_tune(128, 2000, m.timer());
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::Ok), int(t.status));
    TEST_ASSERT_FLOAT_WITHIN(60.0, 2000.0, m.ms(t.params));
}

void test_tune_reports_slow_and_fast_clamps() {
    Model slow;
    slow.us_per_kib_pass = 20000;  // 128 KiB x 3 passes already takes ~7.7 s
    KdfTuning s = kdf_tune(128, 2000, slow.timer());
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::RaisedToMin), int(s.status));
    TEST_ASSERT_EQUAL(kMinKdfPasses, s.params.passes);
    TEST_ASSERT_EQUAL(2, slow.calls);  // no second probe
    TEST_ASSERT_UINT32_WITHIN(100, 7700, kdf_estimate_ms(s));

    Model fast;
    fast.us_per_kib_pass = 0.05;
    fast.overhead_us = 100;
    KdfTuning f = kdf_tune(128, 2000, fast.timer());
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::CappedAtMax), int(f.status));
    TEST_ASSERT_EQUAL(kdf_max_passes(128), f.params.passes);
    TEST_ASSERT_TRUE(kdf_params_valid(f.params));
}

void test_tune_probes_stay_short_on_slow_devices() {
    // 3 passes take 1.2 s: probe B must not add 8 passes (3.2 s) on top.
    Model m;
    m.overhead_us = 0;
    m.us_per_kib_pass = 400000.0 / 128;
    KdfTuning t = kdf_tune(128, 2000, m.timer());
    TEST_ASSERT_TRUE(t.probe_b_us <= 2000000);
    TEST_ASSERT_TRUE(m.total_us <= 4100000);
    TEST_ASSERT_EQUAL(int(KdfTuneStatus::Ok), int(t.status));
    TEST_ASSERT_EQUAL(5, t.params.passes);
}

void test_tune_fails_when_a_derive_cannot_run() {
    for (int fail_at = 1; fail_at <= 3; fail_at++) {
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
    KdfTuning t = kdf_tune(128, 2000, [&](const KdfParams&) {
        return ++calls <= 2 ? 100000u : 90000u;
    });
    TEST_ASSERT_TRUE(t.status != KdfTuneStatus::Failed);
    TEST_ASSERT_TRUE(kdf_params_valid(t.params));
}

void test_check_in_band_changes_nothing() {
    Model m;
    KdfTuning t = kdf_tune(128, 2000, m.timer());
    KdfParams before = t.params;
    int calls = m.calls;
    TEST_ASSERT_FALSE(kdf_check(t, 2400000, 2000, m.timer()));
    TEST_ASSERT_FALSE(kdf_check(t, 1600000, 2000, m.timer()));
    TEST_ASSERT_EQUAL(calls, m.calls);
    TEST_ASSERT_EQUAL(before.passes, t.params.passes);
    TEST_ASSERT_FALSE(t.retuned);
}

void test_check_rescales_a_confirmed_slow_derive() {
    Model m;
    KdfTuning t = kdf_tune(128, 2000, m.timer());
    uint32_t passes = t.params.passes;
    m.us_per_kib_pass *= 2;  // the real derive turns out twice as slow
    TEST_ASSERT_TRUE(kdf_check(t, uint32_t(m.ms(t.params) * 1000), 2000, m.timer()));
    TEST_ASSERT_TRUE(t.retuned);
    TEST_ASSERT_UINT32_WITHIN(passes / 10, passes / 2, t.params.passes);
    TEST_ASSERT_FLOAT_WITHIN(150.0, 2000.0, m.ms(t.params));
    // The estimate follows the retune.
    TEST_ASSERT_UINT32_WITHIN(150, uint32_t(m.ms(t.params)), kdf_estimate_ms(t));
    // Once only.
    TEST_ASSERT_FALSE(kdf_check(t, 4000000, 2000, m.timer()));
}

void test_check_ignores_a_one_off_stall() {
    Model m;
    KdfTuning t = kdf_tune(128, 2000, m.timer());
    uint32_t passes = t.params.passes;
    int calls = m.calls;
    TEST_ASSERT_FALSE(kdf_check(t, 2900000, 2000, m.timer()));  // stalled; the re-run is fine
    TEST_ASSERT_EQUAL(calls + 1, m.calls);
    TEST_ASSERT_EQUAL(passes, t.params.passes);
    TEST_ASSERT_FALSE(t.retuned);
}

void test_check_raises_a_fast_derive_without_rerun() {
    Model m;
    KdfTuning t = kdf_tune(128, 2000, m.timer());
    uint32_t passes = t.params.passes;
    int calls = m.calls;
    TEST_ASSERT_TRUE(kdf_check(t, 1000000, 2000, m.timer()));  // a fast reading is no stall
    TEST_ASSERT_EQUAL(calls, m.calls);
    TEST_ASSERT_TRUE(t.params.passes > passes * 3 / 2);
    TEST_ASSERT_TRUE(kdf_params_valid(t.params));
}

void test_check_leaves_clamped_tunings_alone() {
    Model slow;
    slow.us_per_kib_pass = 20000;
    KdfTuning t = kdf_tune(128, 2000, slow.timer());
    int calls = slow.calls;
    TEST_ASSERT_FALSE(kdf_check(t, 7700000, 2000, slow.timer()));
    TEST_ASSERT_FALSE(t.retuned);
    TEST_ASSERT_EQUAL(calls, slow.calls);
    TEST_ASSERT_EQUAL(7700000, t.check_us);
}

void test_pick_mem_follows_heap() {
    TEST_ASSERT_EQUAL(224, kdf_pick_mem(320 * 1024));
    TEST_ASSERT_EQUAL(128, kdf_pick_mem(250 * 1024));
    TEST_ASSERT_EQUAL(128, kdf_pick_mem(224 * 1024));
    TEST_ASSERT_EQUAL(96, kdf_pick_mem(200 * 1024));
    TEST_ASSERT_EQUAL(64, kdf_pick_mem(160 * 1024));
    TEST_ASSERT_EQUAL(64, kdf_pick_mem(0));
    TEST_ASSERT_EQUAL(4096, kdf_pick_mem(size_t(64) * 1024 * 1024));
    for (size_t kib = 0; kib < 9000; kib += 7) {
        uint32_t mem = kdf_pick_mem(kib * 1024);
        TEST_ASSERT_EQUAL(0, mem % 8);
        TEST_ASSERT_TRUE(mem >= kMinKdfMemKib && mem <= kMaxKdfMemKib);
        // Above the format minimum, the full reserve always stays free.
        if (mem > kMinKdfMemKib) TEST_ASSERT_TRUE(mem * 1024 + kKdfHeapReserve <= kib * 1024);
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
    RUN_TEST(test_argon2id_rfc9106_vector);
    RUN_TEST(test_kdf_derive_known_answer);
    RUN_TEST(test_kdf_field_cap_edges);
    RUN_TEST(test_kdf_work_cap_edges);
    RUN_TEST(test_tune_hits_target_and_estimates_overhead);
    RUN_TEST(test_tune_ignores_cold_start);
    RUN_TEST(test_tune_reports_slow_and_fast_clamps);
    RUN_TEST(test_tune_probes_stay_short_on_slow_devices);
    RUN_TEST(test_tune_fails_when_a_derive_cannot_run);
    RUN_TEST(test_tune_survives_noisy_probes);
    RUN_TEST(test_check_in_band_changes_nothing);
    RUN_TEST(test_check_rescales_a_confirmed_slow_derive);
    RUN_TEST(test_check_ignores_a_one_off_stall);
    RUN_TEST(test_check_raises_a_fast_derive_without_rerun);
    RUN_TEST(test_check_leaves_clamped_tunings_alone);
    RUN_TEST(test_pick_mem_follows_heap);
    RUN_TEST(test_rng_uniform_range_and_variety);
    RUN_TEST(test_wiping_containers);
    return UNITY_END();
}

#include <unity.h>

#include <cstring>

#include "../common/test_util.h"
#include "crypto/kdf.h"
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
    RUN_TEST(test_rng_uniform_range_and_variety);
    RUN_TEST(test_wiping_containers);
    return UNITY_END();
}

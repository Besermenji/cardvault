// Signed SD firmware updates (#85): Ed25519, the .cvu header, version rules,
// the streamed image check and the partition-layout check.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "core/update.h"
#include "monocypher-ed25519.h"

#define CV_UPDATE_TEST_KEY 1
#include "platform/update_key.h"

#include "fixture_cvu.h"

using namespace cv;

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> unhex(const char* h) {
    std::vector<uint8_t> out;
    for (; h[0] && h[1]; h += 2) {
        unsigned v;
        sscanf(h, "%2x", &v);
        out.push_back(uint8_t(v));
    }
    return out;
}

static Version ver(const char* s) {
    Version v;
    TEST_ASSERT_TRUE_MESSAGE(parse_version(s, v), s);
    return v;
}

// ---- Ed25519 (RFC 8032, section 7.1) ------------------------------------------

void test_ed25519_rfc8032_vectors() {
    struct V {
        const char *seed, *pub, *msg, *sig;
    } vs[] = {
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
         "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39"
         "701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
         "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f36"
         "13d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
    };
    for (const V& v : vs) {
        auto seed = unhex(v.seed), pub = unhex(v.pub), msg = unhex(v.msg), sig = unhex(v.sig);
        uint8_t sk[64], pk[32], got[64];
        crypto_ed25519_key_pair(sk, pk, seed.data());
        TEST_ASSERT_EQUAL_MEMORY(pub.data(), pk, 32);
        crypto_ed25519_sign(got, sk, msg.data(), msg.size());
        TEST_ASSERT_EQUAL_MEMORY(sig.data(), got, 64);
        TEST_ASSERT_EQUAL(0, crypto_ed25519_check(sig.data(), pub.data(), msg.data(), msg.size()));
        sig[10] ^= 1;
        TEST_ASSERT_NOT_EQUAL(0, crypto_ed25519_check(sig.data(), pub.data(), msg.data(), msg.size()));
    }
}

// ---- Versions -------------------------------------------------------------------

void test_version_parsing() {
    for (const char* ok : {"0.0.0", "0.3.0", "1.2.3", "255.255.255", "0.4.0-rc1", "1.0.0-rc254"})
        ver(ok);
    Version v;
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.2.3-", "1.2.3-rc", "1.2.3-rc0",
                            "1.2.3-rc01", "1.2.3-rc255", "1.2.3-beta1", "256.0.0", "1.2.3 ",
                            " 1.2.3", "1.2.-3", "1.2.3-RC1", "1..3", "1.2.3x", "1000.0.0"})
        TEST_ASSERT_FALSE_MESSAGE(parse_version(bad, v), bad);
}

void test_version_order() {
    const char* ascending[] = {"0.2.0", "0.3.0", "0.3.1", "0.4.0-rc1", "0.4.0-rc2", "0.4.0",
                               "0.10.0", "1.0.0-rc1", "1.0.0"};
    const int n = sizeof ascending / sizeof ascending[0];
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL(0, compare_versions(ver(ascending[i]), ver(ascending[i])));
        for (int j = i + 1; j < n; j++) {
            TEST_ASSERT_TRUE(compare_versions(ver(ascending[i]), ver(ascending[j])) < 0);
            TEST_ASSERT_TRUE(compare_versions(ver(ascending[j]), ver(ascending[i])) > 0);
        }
        Version v = ver(ascending[i]);
        TEST_ASSERT_EQUAL_STRING(ascending[i], Version::unpack(v.packed()).str().c_str());
    }
}

// ---- Header ---------------------------------------------------------------------

static const uint8_t kSeed[32] = {7, 7, 7};

struct Signed {
    std::vector<uint8_t> file;
    uint8_t pub[32];
};

// A .cvu signed here, with any field set as a test needs (including wrong ones).
static Signed make_file(const char* version, const std::vector<uint8_t>& image,
                        const char* target = kUpdateTarget, uint8_t format = kUpdateFormat,
                        uint32_t len_override = 0xFFFFFFFF, uint32_t reserved = 0) {
    Signed s;
    std::vector<uint8_t>& f = s.file;
    f.assign(kUpdateHeaderSize, 0);
    memcpy(f.data(), "CVUP", 4);
    f[4] = format;
    memcpy(f.data() + 8, version, strnlen(version, 16));
    memcpy(f.data() + 24, target, strnlen(target, 16));
    uint32_t len = len_override == 0xFFFFFFFF ? uint32_t(image.size()) : len_override;
    for (int i = 0; i < 4; i++) {
        f[40 + i] = uint8_t(len >> (8 * i));
        f[44 + i] = uint8_t(reserved >> (8 * i));
    }
    crypto_sha512(f.data() + 48, image.data(), image.size());
    uint8_t sk[64];
    uint8_t seed[32];
    memcpy(seed, kSeed, 32);
    crypto_ed25519_key_pair(sk, s.pub, seed);
    crypto_ed25519_sign(f.data() + kUpdateSignedSize, sk, f.data(), kUpdateSignedSize);
    f.insert(f.end(), image.begin(), image.end());
    return s;
}

static std::vector<uint8_t> image(size_t n = 1000) {
    std::vector<uint8_t> img(n);
    for (size_t i = 0; i < n; i++) img[i] = uint8_t(i * 31 + 7);
    img[0] = 0xE9;
    return img;
}

static UpdateError check(const Signed& s, const char* running = "0.3.0", uint32_t slot = 0x300000) {
    UpdateHeader h;
    return check_update_header(s.file.data(), s.file.size(), s.pub, ver(running), slot, h);
}

void test_openssl_signed_fixture_is_accepted() {
    UpdateHeader h;
    TEST_ASSERT_EQUAL(int(UpdateError::Ok),
                      int(check_update_header(kFixtureCvu, sizeof kFixtureCvu, kUpdatePublicKey,
                                              ver("0.3.0"), 0x300000, h)));
    TEST_ASSERT_EQUAL_STRING("0.4.0", h.version.str().c_str());
    TEST_ASSERT_EQUAL(64, h.image_len);
    UpdateImageCheck c(h);
    TEST_ASSERT_TRUE(c.add(kFixtureCvu + kUpdateHeaderSize, sizeof kFixtureCvu - kUpdateHeaderSize));
    TEST_ASSERT_EQUAL(int(UpdateError::Ok), int(c.finish()));
}

void test_header_accepts_same_or_newer_version() {
    TEST_ASSERT_EQUAL(int(UpdateError::Ok), int(check(make_file("0.3.0", image()))));
    TEST_ASSERT_EQUAL(int(UpdateError::Ok), int(check(make_file("0.4.0-rc1", image()))));
    TEST_ASSERT_EQUAL(int(UpdateError::Ok), int(check(make_file("1.0.0", image()))));
}

void test_header_refuses_older_version() {
    TEST_ASSERT_EQUAL(int(UpdateError::TooOld), int(check(make_file("0.2.0", image()))));
    TEST_ASSERT_EQUAL(int(UpdateError::TooOld), int(check(make_file("0.4.0-rc1", image()), "0.4.0")));
    TEST_ASSERT_EQUAL(int(UpdateError::TooOld), int(check(make_file("0.3.9", image()), "0.4.0-rc1")));
}

void test_header_refuses_wrong_key_and_tampering() {
    Signed s = make_file("0.4.0", image());
    UpdateHeader h;
    TEST_ASSERT_EQUAL(int(UpdateError::BadSignature),
                      int(check_update_header(s.file.data(), s.file.size(), kUpdatePublicKey,
                                              ver("0.3.0"), 0x300000, h)));
    // Every signed byte matters, including the hash and the reserved field.
    for (size_t i : {size_t(8), size_t(9), size_t(24), size_t(40), size_t(44), size_t(48),
                     size_t(111), size_t(112), size_t(175)}) {
        Signed t = s;
        t.file[i] ^= 0x01;
        TEST_ASSERT_EQUAL_MESSAGE(int(UpdateError::BadSignature), int(check(t)),
                                  std::to_string(i).c_str());
    }
}

void test_header_refuses_malformed_files() {
    Signed s = make_file("0.4.0", image());
    UpdateHeader h;
    // Too short, wrong magic, unknown format: refused before the signature.
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile),
                      int(check_update_header(s.file.data(), kUpdateHeaderSize - 1, s.pub,
                                              ver("0.3.0"), 0x300000, h)));
    Signed m = s;
    m.file[0] = 'X';
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile), int(check(m)));
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile), int(check(make_file("0.4.0", image(), kUpdateTarget, 2))));
    // Signed but malformed: still refused.
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile), int(check(make_file("0.4", image()))));
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile), int(check(make_file("0.4.0-beta1", image()))));
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile), int(check(make_file("0.4.0.0.0.0.0.01", image()))));
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile), int(check(make_file("", image()))));
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile),
                      int(check(make_file("0.4.0", image(), kUpdateTarget, 1, 0))));
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile),
                      int(check(make_file("0.4.0", image(), kUpdateTarget, 1, 0xFFFFFFFF, 1))));
}

void test_header_refuses_other_device_and_oversize() {
    TEST_ASSERT_EQUAL(int(UpdateError::WrongDevice), int(check(make_file("0.4.0", image(), "cardputer"))));
    TEST_ASSERT_EQUAL(int(UpdateError::TooBig), int(check(make_file("0.4.0", image(1000)), "0.3.0", 999)));
    TEST_ASSERT_EQUAL(int(UpdateError::Ok), int(check(make_file("0.4.0", image(1000)), "0.3.0", 1000)));
}

// ---- Image stream ---------------------------------------------------------------

static UpdateError stream(const std::vector<uint8_t>& file, size_t chunk, bool* overflow = nullptr) {
    UpdateHeader h;
    uint8_t pub[32];
    uint8_t sk[64], seed[32];
    memcpy(seed, kSeed, 32);
    crypto_ed25519_key_pair(sk, pub, seed);
    UpdateError e = check_update_header(file.data(), file.size(), pub, ver("0.3.0"), 0x300000, h);
    if (e != UpdateError::Ok) return e;
    UpdateImageCheck c(h);
    bool ok = true;
    for (size_t off = kUpdateHeaderSize; off < file.size(); off += chunk)
        ok &= c.add(file.data() + off, std::min(chunk, file.size() - off));
    if (overflow) *overflow = !ok;
    return c.finish();
}

void test_image_stream_checks_every_byte() {
    std::vector<uint8_t> f = make_file("0.4.0", image(10000)).file;
    for (size_t chunk : {size_t(1), size_t(64), size_t(4096), size_t(100000)})
        TEST_ASSERT_EQUAL(int(UpdateError::Ok), int(stream(f, chunk)));
    for (size_t i : {kUpdateHeaderSize, kUpdateHeaderSize + 5000, f.size() - 1}) {
        std::vector<uint8_t> t = f;
        t[i] ^= 0x80;
        TEST_ASSERT_EQUAL(int(UpdateError::HashMismatch), int(stream(t, 4096)));
    }
}

void test_image_stream_detects_truncation_and_extra_bytes() {
    std::vector<uint8_t> f = make_file("0.4.0", image(10000)).file;
    std::vector<uint8_t> shorter(f.begin(), f.end() - 1);
    TEST_ASSERT_EQUAL(int(UpdateError::Truncated), int(stream(shorter, 4096)));
    std::vector<uint8_t> header_only(f.begin(), f.begin() + kUpdateHeaderSize);
    TEST_ASSERT_EQUAL(int(UpdateError::Truncated), int(stream(header_only, 4096)));
    std::vector<uint8_t> longer = f;
    longer.push_back(0);
    bool overflow = false;
    TEST_ASSERT_EQUAL(int(UpdateError::BadFile), int(stream(longer, 4096, &overflow)));
    TEST_ASSERT_TRUE(overflow);
}

// ---- Partition layout -------------------------------------------------------------

// Parses a PlatformIO partition CSV into what ESP-IDF would report.
static std::vector<PartitionInfo> read_csv(const char* path) {
    std::ifstream in(path);
    TEST_ASSERT_TRUE_MESSAGE(in.good(), path);
    std::vector<PartitionInfo> out;
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) {
            c.erase(0, c.find_first_not_of(" \t"));
            c.erase(c.find_last_not_of(" \t\r") + 1);
            cols.push_back(c);
        }
        if (cols.size() < 5 || cols[0].empty()) continue;
        PartitionInfo p;
        p.label = cols[0];
        p.type = cols[1] == "app" ? 0 : 1;
        const std::string& st = cols[2];
        p.subtype = st == "nvs" ? 0x02 : st == "ota" ? 0x00 : st == "spiffs" ? 0x82
                  : st == "ota_0" ? 0x10 : st == "ota_1" ? 0x11 : 0xFF;
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0xFF, p.subtype, st.c_str());
        p.address = uint32_t(std::stoul(cols[3], nullptr, 0));
        p.size = uint32_t(std::stoul(cols[4], nullptr, 0));
        out.push_back(p);
    }
    return out;
}

void test_built_in_layouts_match_the_csv_files() {
    TEST_ASSERT_EQUAL(int(LayoutCheck::Ok), int(classify_layout(read_csv("partitions.csv"), release_layout())));
    TEST_ASSERT_EQUAL(int(LayoutCheck::Ok),
                      int(classify_layout(read_csv("partitions_test.csv"), test_layout())));
}

void test_layout_classification() {
    std::vector<PartitionInfo> own = release_layout();
    std::vector<PartitionInfo> shuffled(own.rbegin(), own.rend());
    TEST_ASSERT_EQUAL(int(LayoutCheck::Ok), int(classify_layout(shuffled, own)));

    std::vector<PartitionInfo> v030;  // CardVault v0.3.0: no app1
    for (const PartitionInfo& p : own)
        if (p.label != "app1") v030.push_back(p);
    TEST_ASSERT_EQUAL(int(LayoutCheck::NoSecondSlot), int(classify_layout(v030, own)));

    // Launcher-like: our rows plus its own app, or our labels somewhere else.
    std::vector<PartitionInfo> launcher = own;
    launcher.push_back({"launcher", 0, 0x00, 0x710000, 0xF0000});
    TEST_ASSERT_EQUAL(int(LayoutCheck::NotOwnTable), int(classify_layout(launcher, own)));
    std::vector<PartitionInfo> moved = own;
    moved[2].address = 0x20000;
    TEST_ASSERT_EQUAL(int(LayoutCheck::NotOwnTable), int(classify_layout(moved, own)));
    std::vector<PartitionInfo> dup = own;
    dup.push_back(own[4]);
    TEST_ASSERT_EQUAL(int(LayoutCheck::NotOwnTable), int(classify_layout(dup, own)));
    TEST_ASSERT_EQUAL(int(LayoutCheck::NotOwnTable), int(classify_layout(test_layout(), own)));
}

void test_update_outcome_after_restart() {
    TEST_ASSERT_EQUAL(int(UpdateOutcome::None), int(update_outcome(false, 1, 1)));
    TEST_ASSERT_EQUAL(int(UpdateOutcome::Installed), int(update_outcome(true, 1, 1)));
    TEST_ASSERT_EQUAL(int(UpdateOutcome::RolledBack), int(update_outcome(true, 1, 0)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ed25519_rfc8032_vectors);
    RUN_TEST(test_version_parsing);
    RUN_TEST(test_version_order);
    RUN_TEST(test_openssl_signed_fixture_is_accepted);
    RUN_TEST(test_header_accepts_same_or_newer_version);
    RUN_TEST(test_header_refuses_older_version);
    RUN_TEST(test_header_refuses_wrong_key_and_tampering);
    RUN_TEST(test_header_refuses_malformed_files);
    RUN_TEST(test_header_refuses_other_device_and_oversize);
    RUN_TEST(test_image_stream_checks_every_byte);
    RUN_TEST(test_image_stream_detects_truncation_and_extra_bytes);
    RUN_TEST(test_built_in_layouts_match_the_csv_files);
    RUN_TEST(test_layout_classification);
    RUN_TEST(test_update_outcome_after_restart);
    return UNITY_END();
}

#include <unity.h>

#include <cstring>

#include "../common/test_util.h"
#include "bip39_vectors.h"
#include "features/base32.h"
#include "features/bip39.h"
#include "features/pwgen.h"
#include "features/totp.h"

using namespace cv;

void setUp() {}
void tearDown() {}

void test_bip39_official_vectors() {
    for (auto& v : kBip39Vectors) {
        std::vector<uint8_t> ent = from_hex(v.entropy_hex);
        SString m;
        TEST_ASSERT_TRUE(bip39_from_entropy(ent.data(), ent.size(), m));
        TEST_ASSERT_EQUAL_STRING(v.mnemonic, m.c_str());
        TEST_ASSERT_EQUAL(int(Bip39Status::Ok), int(bip39_validate(m)));
    }
}

void test_bip39_validation_errors() {
    SString bad_cs = "abandon abandon abandon abandon abandon abandon abandon abandon "
                     "abandon abandon abandon abandon";
    TEST_ASSERT_EQUAL(int(Bip39Status::BadChecksum), int(bip39_validate(bad_cs)));
    int pos = -1;
    SString unknown = "abandon abandon abandon abandon abandon abandon abandon abandon "
                      "abandon bitcoin abandon about";
    TEST_ASSERT_EQUAL(int(Bip39Status::UnknownWord), int(bip39_validate(unknown, &pos)));
    TEST_ASSERT_EQUAL(9, pos);
    TEST_ASSERT_EQUAL(int(Bip39Status::BadWordCount), int(bip39_validate("abandon about")));
    // Extra whitespace is tolerated.
    SString spaced = "  abandon abandon abandon abandon abandon abandon\nabandon abandon "
                     "abandon abandon abandon   about ";
    TEST_ASSERT_EQUAL(int(Bip39Status::Ok), int(bip39_validate(spaced)));
}

void test_bip39_lookup_and_complete() {
    TEST_ASSERT_EQUAL(0, bip39_index("abandon"));
    TEST_ASSERT_EQUAL(2047, bip39_index("zoo"));
    TEST_ASSERT_EQUAL(-1, bip39_index("bitcoin"));
    int out[8];
    TEST_ASSERT_EQUAL(1, bip39_complete("aban", out, 8));
    TEST_ASSERT_EQUAL(0, out[0]);
    int n = bip39_complete("zo", out, 8);
    TEST_ASSERT_EQUAL(2, n);  // zone, zoo
    TEST_ASSERT_EQUAL(0, bip39_complete("qq", out, 8));
    // Every word is uniquely identified by its first four letters.
    for (int i = 0; i < 2048; i++) {
        char pre[5] = {0};
        strncpy(pre, kBip39Words[i], 4);
        int m[4];
        int c = bip39_complete(pre, m, 4);
        if (strlen(kBip39Words[i]) >= 4) TEST_ASSERT_EQUAL(1, c);
    }
}

void test_bip39_generate() {
    for (int words : {12, 15, 18, 21, 24}) {
        SString m;
        TEST_ASSERT_TRUE(bip39_generate(words, m));
        TEST_ASSERT_EQUAL(words, int(bip39_split(m).size()));
        TEST_ASSERT_EQUAL(int(Bip39Status::Ok), int(bip39_validate(m)));
    }
    SString m;
    TEST_ASSERT_FALSE(bip39_generate(13, m));
}

void test_base32_rfc4648() {
    struct {
        const char* in;
        const char* out;
    } v[] = {{"", ""},           {"MY======", "f"},        {"MZXQ====", "fo"},
             {"MZXW6===", "foo"}, {"MZXW6YQ=", "foob"},    {"MZXW6YTB", "fooba"},
             {"MZXW6YTBOI======", "foobar"}, {"mzxw 6ytb-oi", "foobar"}};
    for (auto& t : v) {
        SBytes b;
        TEST_ASSERT_TRUE(base32_decode(t.in, b));
        TEST_ASSERT_EQUAL(strlen(t.out), b.size());
        if (b.size()) TEST_ASSERT_EQUAL_MEMORY(t.out, b.data(), b.size());
    }
    SBytes b;
    TEST_ASSERT_FALSE(base32_decode("MZ1W", b));
    TEST_ASSERT_FALSE(base32_decode("MY==A", b));
}

void test_hotp_rfc4226() {
    const uint8_t* key = reinterpret_cast<const uint8_t*>("12345678901234567890");
    const uint32_t expect[] = {755224, 287082, 359152, 969429, 338314,
                               254676, 287922, 162583, 399871, 520489};
    for (int i = 0; i < 10; i++) TEST_ASSERT_EQUAL(expect[i], hotp(key, 20, i, 6));
}

void test_totp_rfc6238_sha1() {
    const uint8_t* key = reinterpret_cast<const uint8_t*>("12345678901234567890");
    struct {
        uint64_t t;
        uint32_t code;
    } v[] = {{59, 94287082},         {1111111109, 7081804},  {1111111111, 14050471},
             {1234567890, 89005924}, {2000000000, 69279037}, {20000000000ULL, 65353130}};
    for (auto& x : v) TEST_ASSERT_EQUAL(x.code, totp(key, 20, x.t, 30, 8));
    char buf[9];
    totp_format(7081804, 8, buf);
    TEST_ASSERT_EQUAL_STRING("07081804", buf);
}

void test_pwgen_password() {
    PwOptions o;
    o.length = 24;
    for (int i = 0; i < 50; i++) {
        SString p = pwgen_password(o);
        TEST_ASSERT_EQUAL(24, p.size());
        TEST_ASSERT_TRUE(p.find_first_of("abcdefghijklmnopqrstuvwxyz") != SString::npos);
        TEST_ASSERT_TRUE(p.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != SString::npos);
        TEST_ASSERT_TRUE(p.find_first_of("0123456789") != SString::npos);
    }
    o.symbols = false;
    o.no_ambiguous = true;
    SString cs = pwgen_charset(o);
    TEST_ASSERT_EQUAL(SString::npos, cs.find_first_of("0O1lI|!"));
    TEST_ASSERT_TRUE(pwgen_password_bits(o) > 100);
    o.lower = o.upper = o.digits = false;
    TEST_ASSERT_TRUE(pwgen_password(o).empty());
}

void test_pwgen_words_and_strength() {
    SString w = pwgen_words(6, '-');
    int dashes = 0;
    for (char c : w) dashes += c == '-';
    TEST_ASSERT_EQUAL(5, dashes);
    TEST_ASSERT_EQUAL_FLOAT(66.0, pwgen_words_bits(6));
    TEST_ASSERT_TRUE(passphrase_strength_bits("aaaaaaaa") < passphrase_strength_bits("q7#Lp2!x"));
    TEST_ASSERT_TRUE(passphrase_strength_bits("") == 0);
    TEST_ASSERT_TRUE(passphrase_strength_bits("correct horse battery staple") > 60);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_bip39_official_vectors);
    RUN_TEST(test_bip39_validation_errors);
    RUN_TEST(test_bip39_lookup_and_complete);
    RUN_TEST(test_bip39_generate);
    RUN_TEST(test_base32_rfc4648);
    RUN_TEST(test_hotp_rfc4226);
    RUN_TEST(test_totp_rfc6238_sha1);
    RUN_TEST(test_pwgen_password);
    RUN_TEST(test_pwgen_words_and_strength);
    return UNITY_END();
}

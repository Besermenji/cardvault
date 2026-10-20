#include <unity.h>

#include <cstring>

#include "vault/serializer.h"
#include "vault/session.h"

using namespace cv;

void setUp() {}
void tearDown() {}

static const KdfParams kFast{64, 3};

static Entry make_entry(EntryType t, const char* title) {
    Entry e;
    e.type = t;
    e.title = title;
    e.created = 1700000000;
    e.updated = 1700000001;
    return e;
}

void test_serializer_round_trip() {
    VaultData d;
    Entry m = make_entry(EntryType::Mnemonic, "Cold wallet");
    m.set(kFieldWords, "abandon abandon abandon abandon abandon abandon abandon abandon "
                       "abandon abandon abandon about");
    m.set(kFieldBip39Pass, "");
    d.add(std::move(m));
    Entry p = make_entry(EntryType::Password, "Email");
    p.set(kFieldUser, "me@example.com");
    p.set(kFieldPassword, "p@ss\nw0rd with \x01 bytes");
    d.add(std::move(p));

    SBytes bytes;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(serialize(d, bytes)));
    VaultData back;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(deserialize(bytes.data(), bytes.size(), back)));
    TEST_ASSERT_EQUAL(3, back.next_id);
    TEST_ASSERT_EQUAL(2, back.entries.size());
    TEST_ASSERT_EQUAL_STRING("Cold wallet", back.entries[0].title.c_str());
    TEST_ASSERT_EQUAL(int(EntryType::Password), int(back.entries[1].type));
    TEST_ASSERT_EQUAL_STRING("p@ss\nw0rd with \x01 bytes",
                             back.entries[1].get(kFieldPassword)->c_str());
    TEST_ASSERT_EQUAL(1700000001u, back.entries[1].updated);
    TEST_ASSERT_NOT_NULL(back.entries[0].get(kFieldBip39Pass));
}

void test_deserialize_rejects_garbage() {
    VaultData d;
    d.add(make_entry(EntryType::Note, "x"));
    SBytes bytes;
    serialize(d, bytes);
    // Truncate
    VaultData out;
    TEST_ASSERT_EQUAL(int(VaultError::Corrupt), int(deserialize(bytes.data(), bytes.size() - 1, out)));
    // Trailing junk
    bytes.push_back(0);
    TEST_ASSERT_EQUAL(int(VaultError::Corrupt), int(deserialize(bytes.data(), bytes.size(), out)));
    TEST_ASSERT_EQUAL(0, out.entries.size());
}

void test_create_unlock_round_trip() {
    MemoryStore store;
    VaultSession s;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.create(store, "hunter2 hunter2", kFast)));
    Entry n = make_entry(EntryType::Note, "Hello");
    n.set(kFieldBody, "World");
    s.data().add(std::move(n));
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.save(store)));
    s.close();
    TEST_ASSERT_FALSE(s.is_open());
    TEST_ASSERT_EQUAL(0, s.data().entries.size());

    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "hunter2 hunter2")));
    TEST_ASSERT_EQUAL(1, s.data().entries.size());
    TEST_ASSERT_EQUAL_STRING("World", s.data().entries[0].get(kFieldBody)->c_str());
}

void test_wrong_passphrase() {
    MemoryStore store;
    VaultSession s;
    s.create(store, "right", kFast);
    s.close();
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(s.unlock(store, "wrong")));
    TEST_ASSERT_FALSE(s.is_open());
}

void test_file_is_padded_and_has_no_plaintext() {
    MemoryStore store;
    VaultSession s;
    s.create(store, "pw", kFast);
    Entry n = make_entry(EntryType::Note, "TitleMarker");
    n.set(kFieldBody, "SecretMarker");
    s.data().add(std::move(n));
    s.save(store);
    size_t body = store.blob.size() - kHeaderSize - kNonceSize - kMacSize;
    TEST_ASSERT_EQUAL(0, body % kPadBlock);
    std::string raw(store.blob.begin(), store.blob.end());
    TEST_ASSERT_EQUAL(std::string::npos, raw.find("SecretMarker"));
    TEST_ASSERT_EQUAL(std::string::npos, raw.find("TitleMarker"));
}

void test_tamper_detection() {
    MemoryStore store;
    VaultSession s;
    s.create(store, "pw", kFast);
    s.data().add(make_entry(EntryType::Note, "x"));
    s.save(store);
    s.close();
    const std::vector<uint8_t> good = store.blob;

    // Flip one bit in the ciphertext body.
    store.blob = good;
    store.blob.back() ^= 1;
    TEST_ASSERT_EQUAL(int(VaultError::Corrupt), int(s.unlock(store, "pw")));
    // Flip one bit in the salt (part of authenticated params).
    store.blob = good;
    store.blob[20] ^= 1;
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(s.unlock(store, "pw")));
    // Flip one bit in the wrapped key's MAC.
    store.blob = good;
    store.blob[100] ^= 1;
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(s.unlock(store, "pw")));
    // Flip the body nonce.
    store.blob = good;
    store.blob[kHeaderSize] ^= 1;
    TEST_ASSERT_EQUAL(int(VaultError::Corrupt), int(s.unlock(store, "pw")));
    // Bad magic / truncation.
    store.blob = good;
    store.blob[0] = 'X';
    TEST_ASSERT_EQUAL(int(VaultError::BadMagic), int(s.unlock(store, "pw")));
    store.blob = good;
    store.blob.resize(50);
    TEST_ASSERT_EQUAL(int(VaultError::Truncated), int(s.unlock(store, "pw")));
    // Original still opens.
    store.blob = good;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "pw")));
}

void test_change_passphrase() {
    MemoryStore store;
    VaultSession s;
    s.create(store, "old", kFast);
    s.data().add(make_entry(EntryType::Note, "keep me"));
    s.save(store);
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.change_passphrase(store, "new", kFast)));
    s.close();
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(s.unlock(store, "old")));
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "new")));
    TEST_ASSERT_EQUAL_STRING("keep me", s.data().entries[0].title.c_str());
    // Saving after the change keeps working with the new key.
    s.data().add(make_entry(EntryType::Note, "second"));
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.save(store)));
    s.close();
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "new")));
    TEST_ASSERT_EQUAL(2, s.data().entries.size());
}

void test_export_and_restore_backup() {
    MemoryStore store;
    VaultSession s;
    s.create(store, "device pass", kFast);
    s.data().add(make_entry(EntryType::Note, "backed up"));
    s.save(store);

    std::vector<uint8_t> image;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.export_image("backup pass", kFast, image)));
    TEST_ASSERT_TRUE(image != store.blob);

    VaultSession r;
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(r.unlock_bytes(image, "device pass")));
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(r.unlock_bytes(image, "backup pass")));
    TEST_ASSERT_EQUAL_STRING("backed up", r.data().entries[0].title.c_str());
}

void test_too_large_rejected() {
    MemoryStore store;
    VaultSession s;
    s.create(store, "pw", kFast);
    for (int i = 0; i < 4; i++) {
        Entry n = make_entry(EntryType::Note, "big");
        n.set(kFieldBody, SString(15000, 'a'));
        s.data().add(std::move(n));
    }
    TEST_ASSERT_EQUAL(int(VaultError::TooLarge), int(s.save(store)));
}

void test_remove_entry() {
    VaultData d;
    uint32_t a = d.add(make_entry(EntryType::Note, "a"));
    uint32_t b = d.add(make_entry(EntryType::Note, "b"));
    TEST_ASSERT_TRUE(d.remove(a));
    TEST_ASSERT_FALSE(d.remove(a));
    TEST_ASSERT_NULL(d.find(a));
    TEST_ASSERT_NOT_NULL(d.find(b));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_serializer_round_trip);
    RUN_TEST(test_deserialize_rejects_garbage);
    RUN_TEST(test_create_unlock_round_trip);
    RUN_TEST(test_wrong_passphrase);
    RUN_TEST(test_file_is_padded_and_has_no_plaintext);
    RUN_TEST(test_tamper_detection);
    RUN_TEST(test_change_passphrase);
    RUN_TEST(test_export_and_restore_backup);
    RUN_TEST(test_too_large_rejected);
    RUN_TEST(test_remove_entry);
    return UNITY_END();
}

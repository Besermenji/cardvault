#include <unity.h>

#include <cstring>

#include "crypto/rng.h"
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
    d.add(m);
    Entry p = make_entry(EntryType::Password, "Email");
    p.set(kFieldUser, "me@example.com");
    p.set(kFieldPassword, "p@ss\nw0rd with \x01 bytes");
    d.add(p);

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
    s.data().add(n);
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
    s.create(store, "right-passphrase", kFast);
    s.close();
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(s.unlock(store, "wrong")));
    TEST_ASSERT_FALSE(s.is_open());
}

void test_file_is_padded_and_has_no_plaintext() {
    MemoryStore store;
    VaultSession s;
    s.create(store, "pw-test-pass", kFast);
    Entry n = make_entry(EntryType::Note, "TitleMarker");
    n.set(kFieldBody, "SecretMarker");
    s.data().add(n);
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
    s.create(store, "pw-test-pass", kFast);
    s.data().add(make_entry(EntryType::Note, "x"));
    s.save(store);
    s.close();
    const std::vector<uint8_t> good = store.blob;

    // Flip one bit in the ciphertext body.
    store.blob = good;
    store.blob.back() ^= 1;
    TEST_ASSERT_EQUAL(int(VaultError::Corrupt), int(s.unlock(store, "pw-test-pass")));
    // Flip one bit in the salt (part of authenticated params).
    store.blob = good;
    store.blob[20] ^= 1;
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(s.unlock(store, "pw-test-pass")));
    // Flip one bit in the wrapped key's MAC.
    store.blob = good;
    store.blob[100] ^= 1;
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(s.unlock(store, "pw-test-pass")));
    // Flip the body nonce.
    store.blob = good;
    store.blob[kHeaderSize] ^= 1;
    TEST_ASSERT_EQUAL(int(VaultError::Corrupt), int(s.unlock(store, "pw-test-pass")));
    // Bad magic / truncation.
    store.blob = good;
    store.blob[0] = 'X';
    TEST_ASSERT_EQUAL(int(VaultError::BadMagic), int(s.unlock(store, "pw-test-pass")));
    store.blob = good;
    store.blob.resize(50);
    TEST_ASSERT_EQUAL(int(VaultError::Truncated), int(s.unlock(store, "pw-test-pass")));
    // Original still opens.
    store.blob = good;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "pw-test-pass")));
}

void test_change_passphrase() {
    MemoryStore store;
    VaultSession s;
    s.create(store, "old-passphrase", kFast);
    s.data().add(make_entry(EntryType::Note, "keep me"));
    s.save(store);
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.change_passphrase(store, "new-passphrase", kFast)));
    s.close();
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(s.unlock(store, "old-passphrase")));
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "new-passphrase")));
    TEST_ASSERT_EQUAL_STRING("keep me", s.data().entries[0].title.c_str());
    // Saving after the change keeps working with the new key.
    s.data().add(make_entry(EntryType::Note, "second"));
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.save(store)));
    s.close();
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "new-passphrase")));
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

void test_passphrase_policy() {
    const SString bad[] = {"", "1234567", SString(129, 'x'), "        ", "\t\n \r \t\n \r"};
    for (const SString& pw : bad) {
        MemoryStore store;
        VaultSession s;
        TEST_ASSERT_EQUAL(int(VaultError::WeakPassphrase), int(s.create(store, pw, kFast)));
        TEST_ASSERT_FALSE(s.is_open());
        TEST_ASSERT_TRUE(store.blob.empty());

        // An open vault refuses the weak passphrase for change and export,
        // and the stored vault keeps its old passphrase.
        TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.create(store, "old-passphrase", kFast)));
        std::vector<uint8_t> before = store.blob;
        TEST_ASSERT_EQUAL(int(VaultError::WeakPassphrase),
                          int(s.change_passphrase(store, pw, kFast)));
        TEST_ASSERT_TRUE(before == store.blob);
        std::vector<uint8_t> image;
        TEST_ASSERT_EQUAL(int(VaultError::WeakPassphrase), int(s.export_image(pw, kFast, image)));
        TEST_ASSERT_TRUE(image.empty());
        s.close();
        TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "old-passphrase")));
    }
    // Boundaries are accepted.
    for (size_t len : {kMinPassphraseLen, kMaxPassphraseLen}) {
        MemoryStore store;
        VaultSession s;
        TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.create(store, SString(len, 'k'), kFast)));
    }
}

// A vault image sealed the way create() did before the policy existed, so it
// can carry a passphrase shorter than 8 characters.
static std::vector<uint8_t> legacy_image(const char* pass) {
    Header h;
    h.kdf = kFast;
    header_new_salt(h);
    uint8_t kek[32], dek[32];
    TEST_ASSERT_TRUE(kdf_derive(kek, reinterpret_cast<const uint8_t*>(pass), strlen(pass), h.salt,
                                h.kdf));
    rng_fill(dek, 32);
    header_wrap(h, kek, dek);
    VaultData d;
    d.add(make_entry(EntryType::Note, "old vault"));
    SBytes payload;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(serialize(d, payload)));
    std::vector<uint8_t> out;
    TEST_ASSERT_EQUAL(int(VaultError::Ok),
                      int(seal_file(h, dek, payload.data(), payload.size(), out)));
    return out;
}

void test_unlock_empty_and_legacy_short_passphrases() {
    // Vaults and backups made before the policy still open with their own
    // short passphrase, from flash and from an image.
    MemoryStore store;
    store.write(legacy_image("pw"));
    VaultSession s;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "pw")));
    TEST_ASSERT_EQUAL_STRING("old vault", s.data().entries[0].title.c_str());
    s.close();
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock_bytes(store.blob, "pw")));
    s.close();
    TEST_ASSERT_EQUAL(int(VaultError::WrongPassphrase), int(s.unlock(store, "pz")));
    // Empty is refused outright, with its own error.
    TEST_ASSERT_EQUAL(int(VaultError::EmptyPassphrase), int(s.unlock_bytes(store.blob, "")));
    TEST_ASSERT_EQUAL(int(VaultError::EmptyPassphrase), int(s.unlock(store, "")));
    TEST_ASSERT_FALSE(s.is_open());
}

void test_too_large_rejected() {
    MemoryStore store;
    VaultSession s;
    s.create(store, "pw-test-pass", kFast);
    for (int i = 0; i < 4; i++) {
        Entry n = make_entry(EntryType::Note, "big");
        n.set(kFieldBody, SString(15000, 'a'));
        s.data().add(n);
    }
    TEST_ASSERT_EQUAL(int(VaultError::TooLarge), int(s.save(store)));
}

static std::vector<uint8_t> sealed_image() {
    MemoryStore st;
    VaultSession v;
    v.create(st, "layout test pass", kFast);
    std::vector<uint8_t> img;
    st.read(img);
    return img;
}

void test_check_layout_accepts_real_image_and_returns_header() {
    auto img = sealed_image();
    Header h;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(check_layout(img.data(), img.size(), &h)));
    TEST_ASSERT_EQUAL(64, h.kdf.mem_kib);
    TEST_ASSERT_EQUAL(3, h.kdf.passes);
}

void test_check_layout_rejects_bad_shapes() {
    auto img = sealed_image();
    TEST_ASSERT_EQUAL(int(VaultError::Truncated), int(check_layout(img.data(), 10)));
    // Header alone, no body.
    TEST_ASSERT_EQUAL(int(VaultError::Truncated), int(check_layout(img.data(), kHeaderSize)));
    // Body not a multiple of the pad block.
    auto odd = img;
    odd.push_back(0);
    TEST_ASSERT_EQUAL(int(VaultError::Corrupt), int(check_layout(odd.data(), odd.size())));
    auto bad_magic = img;
    bad_magic[0] ^= 0xFF;
    TEST_ASSERT_EQUAL(int(VaultError::BadMagic),
                      int(check_layout(bad_magic.data(), bad_magic.size())));
}

void test_check_layout_size_cap_edges() {
    auto img = sealed_image();
    // Exactly the largest legal file passes the layout check.
    std::vector<uint8_t> max_img(img.begin(), img.begin() + kHeaderSize);
    max_img.resize(kMaxFileSize, 0);
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(check_layout(max_img.data(), max_img.size())));
    // One pad block more is refused as too large, before any parsing.
    max_img.resize(kMaxFileSize + kPadBlock, 0);
    TEST_ASSERT_EQUAL(int(VaultError::TooLarge),
                      int(check_layout(max_img.data(), max_img.size())));
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

struct CountingStore : MemoryStore {
    int writes = 0;
    bool write(const std::vector<uint8_t>& data) override {
        writes++;
        return MemoryStore::write(data);
    }
};

// --- secret hygiene ------------------------------------------------------------

void test_wipe_string_clears_inline_buffer_and_dead_bytes() {
    SString s("hunter2-secret");  // fits the small-string buffer
    const char* buf = s.data();
    s.pop_back();
    s.pop_back();  // bytes past size() still hold secret characters
    wipe(s);
    TEST_ASSERT_TRUE(s.empty());
    // The inline buffer lives in the object; shrink_to_fit keeps it in place.
    if (s.data() == buf) {
        for (size_t i = 0; i < 15; i++) TEST_ASSERT_EQUAL_UINT8(0, uint8_t(buf[i]));
    }
}

void test_serialized_size_matches_serialize() {
    VaultData d;
    SBytes out;
    serialize(d, out);
    TEST_ASSERT_EQUAL(out.size(), serialized_size(d));  // empty vault
    Entry m = make_entry(EntryType::Mnemonic, "Cold wallet");
    m.set(kFieldWords, "abandon abandon about");
    m.set(kFieldBip39Pass, "");
    d.add(m);
    Entry p = make_entry(EntryType::Password, "Mail \xC3\xA9");
    p.set(kFieldUser, "me");
    p.set(kFieldPassword, SString(300, 'p'));
    d.add(p);
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(serialize(d, out)));
    TEST_ASSERT_EQUAL(out.size(), serialized_size(d));
    size_t per_entry = 0;
    for (auto& e : d.entries) per_entry += entry_serialized_size(e);
    TEST_ASSERT_EQUAL(out.size(), per_entry + 7);
}

void test_entry_value_returns_reference_or_empty() {
    Entry e = make_entry(EntryType::Password, "x");
    e.set(kFieldPassword, "pw");
    TEST_ASSERT_EQUAL_PTR(e.get(kFieldPassword), &e.value(kFieldPassword));
    TEST_ASSERT_TRUE(e.value(kFieldUrl).empty());
}

void test_vault_add_copies_and_keeps_source() {
    VaultData d;
    Entry n = make_entry(EntryType::Note, "n");
    n.set(kFieldBody, "text");
    uint32_t id = d.add(n);
    TEST_ASSERT_EQUAL(1, id);
    TEST_ASSERT_EQUAL(2, d.next_id);
    TEST_ASSERT_EQUAL_STRING("text", n.value(kFieldBody).c_str());  // source untouched
    TEST_ASSERT_EQUAL(id, d.entries[0].id);
    TEST_ASSERT_EQUAL(0, n.id);
}

// True when no byte of the string's buffer (live part or not) is set.
static bool buffer_zero(const SString& str) {
    const char* p = str.data();
    for (size_t i = 0; i < str.capacity(); i++)
        if (p[i]) return false;
    return true;
}

void test_entry_move_wipes_source() {
    Entry a = make_entry(EntryType::Password, "short-title");  // inline buffer
    a.set(kFieldPassword, "pw");
    Entry b(std::move(a));
    TEST_ASSERT_EQUAL_STRING("short-title", b.title.c_str());
    TEST_ASSERT_EQUAL_STRING("pw", b.value(kFieldPassword).c_str());
    TEST_ASSERT_TRUE(a.title.empty() && a.fields.empty());
    TEST_ASSERT_TRUE(buffer_zero(a.title));

    Entry c = make_entry(EntryType::Note, "older-longer-title");
    c = std::move(b);
    TEST_ASSERT_EQUAL_STRING("short-title", c.title.c_str());
    TEST_ASSERT_TRUE(b.title.empty() && b.fields.empty());
    TEST_ASSERT_TRUE(buffer_zero(b.title));
}

void test_entry_copy_assign_leaves_no_old_bytes() {
    Entry dst = make_entry(EntryType::Note, "old-secret-ttl");  // 14 chars, inline
    Entry src = make_entry(EntryType::Note, "ab");
    dst = src;
    TEST_ASSERT_EQUAL_STRING("ab", dst.title.c_str());
    const char* p = dst.title.data();
    for (size_t i = dst.title.size(); i < dst.title.capacity(); i++)
        TEST_ASSERT_EQUAL_UINT8(0, uint8_t(p[i]));
}

void test_remove_shifts_entries_without_leftover_copies() {
    VaultData d;
    const char* titles[] = {"gone-secret", "keep-one", "keep-two"};
    for (const char* t : titles) d.add(make_entry(EntryType::Note, t));
    d.remove(d.entries[0].id);
    TEST_ASSERT_EQUAL(2, d.entries.size());
    TEST_ASSERT_EQUAL_STRING("keep-one", d.entries[0].title.c_str());
    TEST_ASSERT_EQUAL_STRING("keep-two", d.entries[1].title.c_str());
    // Shifted slots were wiped before being refilled: nothing of a longer
    // previous title survives past the end of a shorter new one.
    for (auto& e : d.entries) {
        const char* p = e.title.data();
        for (size_t i = e.title.size(); i < e.title.capacity(); i++)
            TEST_ASSERT_EQUAL_UINT8(0, uint8_t(p[i]));
    }
}

void test_kdf_check_retunes_before_writing() {
    CountingStore store;
    VaultSession s;
    int calls = 0;
    uint32_t seen_us = 0;
    KdfCheck check = [&](uint32_t us, KdfParams& kdf) {
        calls++;
        seen_us = us;
        kdf.passes = 4;
        return true;
    };
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.create(store, "hunter2 hunter2", kFast, check)));
    TEST_ASSERT_EQUAL(1, calls);
    TEST_ASSERT_TRUE(seen_us > 0);
    TEST_ASSERT_EQUAL(4, s.header().kdf.passes);
    TEST_ASSERT_EQUAL(1, store.writes);  // one file, never a copy with the old params
    s.close();
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.unlock(store, "hunter2 hunter2")));
    TEST_ASSERT_EQUAL(4, s.header().kdf.passes);

    // Change passphrase uses the same loop; an invalid suggestion is ignored.
    KdfCheck bad = [&](uint32_t, KdfParams& kdf) {
        calls++;
        kdf.passes = 1;
        return true;
    };
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(s.change_passphrase(store, "second passphrase", kFast, bad)));
    TEST_ASSERT_EQUAL(2, calls);
    TEST_ASSERT_EQUAL(kFast.passes, s.header().kdf.passes);
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
    RUN_TEST(test_passphrase_policy);
    RUN_TEST(test_unlock_empty_and_legacy_short_passphrases);
    RUN_TEST(test_too_large_rejected);
    RUN_TEST(test_check_layout_accepts_real_image_and_returns_header);
    RUN_TEST(test_check_layout_rejects_bad_shapes);
    RUN_TEST(test_check_layout_size_cap_edges);
    RUN_TEST(test_remove_entry);
    RUN_TEST(test_kdf_check_retunes_before_writing);
    RUN_TEST(test_wipe_string_clears_inline_buffer_and_dead_bytes);
    RUN_TEST(test_entry_value_returns_reference_or_empty);
    RUN_TEST(test_serialized_size_matches_serialize);
    RUN_TEST(test_vault_add_copies_and_keeps_source);
    RUN_TEST(test_entry_move_wipes_source);
    RUN_TEST(test_entry_copy_assign_leaves_no_old_bytes);
    RUN_TEST(test_remove_shifts_entries_without_leftover_copies);
    return UNITY_END();
}

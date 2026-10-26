// Navigation (router + routing decisions) and security flows.
#include <unity.h>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "core/flows.h"
#include "core/messages.h"
#include "crypto/kdf.h"
#include "vault/serializer.h"

using namespace cv;
using namespace cv::ui;

void setUp() {}
void tearDown() {}

// ---- Fake screens -------------------------------------------------------------

static std::vector<std::string> g_log;

class FakeScreen : public Screen {
public:
    explicit FakeScreen(const char* n) : n_(n) { g_log.push_back(std::string("new ") + n_); }
    ~FakeScreen() override { g_log.push_back(std::string("del ") + n_); }
    const char* name() const override { return n_; }
    void on_key(const Key& k) override {
        if (on_key_fn) on_key_fn(k);
    }
    void draw(Canvas&) override {}
    void on_resume() override {
        resumed++;
        Screen::on_resume();
    }
    std::function<void(const Key&)> on_key_fn;
    int resumed = 0;

private:
    const char* n_;
};

static ScreenPtr fake(const char* n) { return ScreenPtr(new FakeScreen(n)); }
static std::string top_name(Router& r) { return r.top() ? r.top()->name() : "<none>"; }

// ---- Router -----------------------------------------------------------------

void test_router_ops_are_deferred_until_apply() {
    Router r;
    r.push(fake("a"));
    TEST_ASSERT_NULL(r.top());
    TEST_ASSERT_EQUAL(1, r.pending());
    r.apply();
    TEST_ASSERT_EQUAL_STRING("a", top_name(r).c_str());
    TEST_ASSERT_EQUAL(0, r.pending());
}

void test_router_push_pop_replace_reset() {
    Router r;
    r.push(fake("home"));
    r.push(fake("view"));
    r.apply();
    TEST_ASSERT_EQUAL(2, r.depth());
    r.replace(fake("edit"));
    r.apply();
    TEST_ASSERT_EQUAL(2, r.depth());
    TEST_ASSERT_EQUAL_STRING("edit", top_name(r).c_str());
    TEST_ASSERT_EQUAL_STRING("home", r.at(0)->name());
    r.reset(fake("unlock"));
    r.apply();
    TEST_ASSERT_EQUAL(1, r.depth());
    TEST_ASSERT_EQUAL_STRING("unlock", top_name(r).c_str());
    r.pop();
    r.pop();  // popping an empty stack is harmless
    r.apply();
    TEST_ASSERT_EQUAL(0, r.depth());
}

void test_router_apply_reports_navigation() {
    Router r;
    TEST_ASSERT_FALSE(r.apply());  // nothing queued
    r.push(fake("a"));
    TEST_ASSERT_TRUE(r.apply());
    TEST_ASSERT_FALSE(r.apply());  // already applied
    r.push(fake("b"));
    r.pop();
    TEST_ASSERT_TRUE(r.apply());
    r.replace(fake("c"));
    TEST_ASSERT_TRUE(r.apply());
    r.reset(fake("d"));
    TEST_ASSERT_TRUE(r.apply());
    TEST_ASSERT_FALSE(r.apply());
}

void test_router_resumes_new_top_and_orders_ops() {
    Router r;
    r.push(fake("home"));
    r.apply();
    auto* home = static_cast<FakeScreen*>(r.top());
    TEST_ASSERT_EQUAL(1, home->resumed);
    r.push(fake("menu"));
    r.apply();
    // Menu pops itself and opens a dialog in the same event: pop then push.
    r.pop();
    r.push(fake("dialog"));
    r.apply();
    TEST_ASSERT_EQUAL(2, r.depth());
    TEST_ASSERT_EQUAL_STRING("dialog", top_name(r).c_str());
    // Dialog closes: home is the top again and is told to redraw.
    home->dirty = false;
    r.pop();
    r.apply();
    TEST_ASSERT_EQUAL_STRING("home", top_name(r).c_str());
    TEST_ASSERT_EQUAL(2, home->resumed);
    TEST_ASSERT_TRUE(home->dirty);
}

void test_router_screen_can_pop_itself_inside_handler() {
    Router r;
    r.push(fake("home"));
    r.push(fake("dialog"));
    r.apply();
    auto* dialog = static_cast<FakeScreen*>(r.top());
    bool still_alive_during_handler = false;
    g_log.clear();
    dialog->on_key_fn = [&](const Key&) {
        r.pop();
        // The screen must not be destroyed while its own handler runs.
        still_alive_during_handler = g_log.empty();
    };
    r.top()->on_key(Key());
    TEST_ASSERT_TRUE(still_alive_during_handler);
    r.apply();
    TEST_ASSERT_EQUAL_STRING("home", top_name(r).c_str());
    TEST_ASSERT_EQUAL_STRING("del dialog", g_log.back().c_str());
}

void test_router_clear_now_destroys_everything_and_drops_pending() {
    Router r;
    r.push(fake("home"));
    r.push(fake("view"));
    r.apply();
    r.push(fake("pending"));
    g_log.clear();
    r.clear_now();
    TEST_ASSERT_EQUAL(0, r.depth());
    TEST_ASSERT_EQUAL(0, r.pending());
    int deletions = 0;
    for (auto& l : g_log) deletions += l.rfind("del ", 0) == 0;
    TEST_ASSERT_EQUAL(3, deletions);  // both stacked screens + the never-pushed one
}

// ---- Wipe -------------------------------------------------------------------

namespace {
// Accepts `writes_left` more writes, then fails every write (-1 = never fails).
struct FlakyKv : MemoryKv {
    int writes_left = -1;
    bool set_u32(const char* key, uint32_t value) override {
        if (writes_left == 0) return false;
        if (writes_left > 0) writes_left--;
        return MemoryKv::set_u32(key, value);
    }
};

// Records the guard's state at the moment the vault is erased.
struct SpyStore : MemoryStore {
    bool fail_erase = false;
    AttemptGuard* guard = nullptr;
    bool exhausted_at_erase = false;
    bool erase_called = false;
    bool erase() override {
        erase_called = true;
        if (guard) exhausted_at_erase = guard->exhausted();
        if (fail_erase) return false;
        return MemoryStore::erase();
    }
};
}  // namespace

void test_wipe_success_clears_vault_and_counter() {
    MemoryKv kv;
    AttemptGuard g(kv);
    SpyStore s;
    s.guard = &g;
    s.write({1, 2, 3});
    g.begin_attempt();
    WipeResult r = wipe_vault(s, g);
    TEST_ASSERT_TRUE(r.armed && r.erased && r.reset);
    TEST_ASSERT_FALSE(s.exists());
    TEST_ASSERT_EQUAL(0, g.failures());
    // The intent to wipe was persisted before anything was destroyed.
    TEST_ASSERT_TRUE(s.exhausted_at_erase);
}

void test_wipe_failure_keeps_counter_exhausted() {
    MemoryKv kv;
    AttemptGuard g(kv);
    SpyStore s;
    s.fail_erase = true;
    s.write({1, 2, 3});
    WipeResult r = wipe_vault(s, g);
    TEST_ASSERT_TRUE(r.armed);
    TEST_ASSERT_FALSE(r.erased);
    TEST_ASSERT_FALSE(r.reset);
    TEST_ASSERT_TRUE(s.exists());
    TEST_ASSERT_TRUE(g.exhausted());
    // The next boot retries the wipe, even if the partition is unmountable.
    TEST_ASSERT_EQUAL(int(BootRoute::Wipe), int(boot_route(true, true, false, g.exhausted(), true)));
}

void test_wipe_reports_counter_that_could_not_be_reset() {
    FlakyKv kv;
    kv.writes_left = 1;  // mark_exhausted succeeds, reset fails
    AttemptGuard g(kv);
    SpyStore s;
    s.write({1, 2, 3});
    WipeResult r = wipe_vault(s, g);
    TEST_ASSERT_TRUE(r.armed);
    TEST_ASSERT_TRUE(r.erased);
    TEST_ASSERT_FALSE(r.reset);  // caller must stop, not reboot into another wipe
    TEST_ASSERT_FALSE(s.exists());
    TEST_ASSERT_TRUE(g.exhausted());
}

void test_wipe_runs_even_if_counter_cannot_persist() {
    MemoryKv kv;
    kv.fail_writes = true;
    AttemptGuard g(kv);
    SpyStore s;
    s.write({1, 2, 3});
    WipeResult r = wipe_vault(s, g);
    TEST_ASSERT_TRUE(s.erase_called);
    TEST_ASSERT_TRUE(r.erased);
    TEST_ASSERT_FALSE(r.armed);
    TEST_ASSERT_FALSE(s.exists());
}

void test_wipe_not_armed_when_erase_and_counter_both_fail() {
    MemoryKv kv;
    AttemptGuard g(kv);
    kv.fail_writes = true;
    SpyStore s;
    s.fail_erase = true;
    s.write({1, 2, 3});
    WipeResult r = wipe_vault(s, g);
    // No retry is scheduled: the UI must not promise one.
    TEST_ASSERT_FALSE(r.armed);
    TEST_ASSERT_FALSE(r.erased);
    TEST_ASSERT_TRUE(s.exists());
}

void test_wipe_armed_by_earlier_failures_even_if_mark_fails() {
    MemoryKv kv;
    AttemptGuard g(kv);
    for (uint32_t i = 0; i < kMaxAttempts; i++) g.begin_attempt();
    kv.fail_writes = true;
    SpyStore s;
    s.fail_erase = true;
    s.write({1, 2, 3});
    WipeResult r = wipe_vault(s, g);
    TEST_ASSERT_TRUE(r.armed);  // ten wrong passphrases already exhausted it
    TEST_ASSERT_FALSE(r.erased);
}

// ---- Boot route -------------------------------------------------------------

void test_boot_route_without_counter_storage_blocks_everything() {
    // No working counter: every attempt would be unguarded, whatever else is true.
    for (int mounted = 0; mounted < 2; mounted++)
        for (int exhausted = 0; exhausted < 2; exhausted++)
            for (int vault = 0; vault < 2; vault++)
                TEST_ASSERT_EQUAL(int(BootRoute::GuardUnavailable),
                                  int(boot_route(true, false, mounted, exhausted, vault)));
    TEST_ASSERT_EQUAL(int(BootRoute::NoPartition), int(boot_route(false, false, true, true, true)));
}

void test_boot_route() {
    TEST_ASSERT_EQUAL(int(BootRoute::NoPartition), int(boot_route(false, true, false, false, false)));
    TEST_ASSERT_EQUAL(int(BootRoute::StorageUnreadable), int(boot_route(true, true, false, false, false)));
    // An exhausted counter beats an unmountable partition: finish the wipe.
    TEST_ASSERT_EQUAL(int(BootRoute::Wipe), int(boot_route(true, true, false, true, true)));
    TEST_ASSERT_EQUAL(int(BootRoute::Wipe), int(boot_route(true, true, false, true, false)));
    TEST_ASSERT_EQUAL(int(BootRoute::NoPartition), int(boot_route(false, true, false, true, false)));
    TEST_ASSERT_EQUAL(int(BootRoute::Wipe), int(boot_route(true, true, true, true, true)));
    TEST_ASSERT_EQUAL(int(BootRoute::Wipe), int(boot_route(true, true, true, true, false)));
    TEST_ASSERT_EQUAL(int(BootRoute::Unlock), int(boot_route(true, true, true, false, true)));
    TEST_ASSERT_EQUAL(int(BootRoute::Setup), int(boot_route(true, true, true, false, false)));
}

// ---- Unlock -----------------------------------------------------------------

static const KdfParams kFast{64, 3};

struct Fixture {
    MemoryStore store;
    MemoryKv kv;
    AttemptGuard guard{kv};
    VaultSession session;
    Fixture() {
        guard.reset();  // setup writes a real 0; a missing counter would read as 5
        session.create(store, "right horse battery", kFast);
        Entry e;
        e.type = EntryType::Note;
        e.title = "n";
        session.data().add(e);
        session.save(store);
        session.close();
    }
};

void test_unlock_success_resets_counter() {
    Fixture f;
    f.guard.begin_attempt();
    f.guard.begin_attempt();
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "right horse battery");
    TEST_ASSERT_EQUAL(int(UnlockResult::Unlocked), int(r.kind));
    TEST_ASSERT_TRUE(f.session.is_open());
    TEST_ASSERT_EQUAL(1, f.session.data().entries.size());
    TEST_ASSERT_EQUAL(0, f.guard.failures());
}

void test_unlock_wrong_counts_and_reports_delay() {
    Fixture f;
    for (uint32_t i = 1; i <= 9; i++) {
        UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "nope");
        TEST_ASSERT_EQUAL(int(UnlockResult::Wrong), int(r.kind));
        TEST_ASSERT_EQUAL(i, f.guard.failures());
        TEST_ASSERT_EQUAL(attempt_delay_ms(i), r.retry_delay_ms);
        TEST_ASSERT_FALSE(f.session.is_open());
    }
    UnlockResult last = unlock_attempt(f.guard, f.session, f.store, "nope");
    TEST_ASSERT_EQUAL(int(UnlockResult::MustWipe), int(last.kind));
    // Even the right passphrase is refused once the limit is reached.
    UnlockResult after = unlock_attempt(f.guard, f.session, f.store, "right horse battery");
    TEST_ASSERT_EQUAL(int(UnlockResult::MustWipe), int(after.kind));
    TEST_ASSERT_FALSE(f.session.is_open());
}

void test_unlock_counter_persisted_before_check() {
    // Simulate a power cut during the KDF: the attempt was already recorded.
    Fixture f;
    TEST_ASSERT_TRUE(f.guard.begin_attempt());
    AttemptGuard after_reboot(f.kv);
    TEST_ASSERT_EQUAL(1, after_reboot.failures());
}

void test_unlock_refused_when_counter_cannot_be_saved() {
    Fixture f;
    f.kv.fail_writes = true;
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "right horse battery");
    TEST_ASSERT_EQUAL(int(UnlockResult::Refused), int(r.kind));
    TEST_ASSERT_FALSE(f.session.is_open());
}

void test_unlock_damaged_vault() {
    Fixture f;
    f.store.blob.back() ^= 1;  // corrupt the ciphertext body
    f.guard.begin_attempt();
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "right horse battery");
    TEST_ASSERT_EQUAL(int(UnlockResult::Damaged), int(r.kind));
    TEST_ASSERT_FALSE(f.session.is_open());
    TEST_ASSERT_EQUAL(0, f.guard.failures());  // passphrase was right
}

void test_unlock_failure_refunds_attempt() {
    Fixture f;
    // A header demanding more Argon2 memory than exists -> OutOfMemory on host? Use
    // a missing file instead (I/O error), which must not cost an attempt.
    f.store.erase();
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "right horse battery");
    TEST_ASSERT_EQUAL(int(UnlockResult::Failed), int(r.kind));
    TEST_ASSERT_EQUAL(int(VaultError::Io), int(r.error));
    TEST_ASSERT_EQUAL(0, f.guard.failures());
}

void test_unlock_empty_passphrase_not_counted() {
    Fixture f;
    for (int i = 0; i < 12; i++) {
        UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "");
        TEST_ASSERT_EQUAL(int(UnlockResult::Empty), int(r.kind));
    }
    TEST_ASSERT_EQUAL(0, f.guard.failures());
    TEST_ASSERT_FALSE(f.session.is_open());
}

void test_unlock_empty_passphrase_with_exhausted_guard_must_wipe() {
    Fixture f;
    for (uint32_t i = 0; i < kMaxAttempts; i++) f.guard.begin_attempt();
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "");
    TEST_ASSERT_EQUAL(int(UnlockResult::MustWipe), int(r.kind));
}

void test_current_passphrase_check_counts_against_guard() {
    // Settings > Change passphrase confirms the current passphrase with a
    // scratch session while the vault stays open.
    Fixture f;
    f.session.unlock(f.store, "right horse battery");
    VaultSession check;
    UnlockResult r = unlock_attempt(f.guard, check, f.store, "guess");
    TEST_ASSERT_EQUAL(int(UnlockResult::Wrong), int(r.kind));
    TEST_ASSERT_EQUAL(1, f.guard.failures());
    TEST_ASSERT_TRUE(f.session.is_open());
    r = unlock_attempt(f.guard, check, f.store, "right horse battery");
    TEST_ASSERT_EQUAL(int(UnlockResult::Unlocked), int(r.kind));
    TEST_ASSERT_EQUAL(0, f.guard.failures());
    check.close();
    for (uint32_t i = 0; i < kMaxAttempts; i++) r = unlock_attempt(f.guard, check, f.store, "guess");
    TEST_ASSERT_EQUAL(int(UnlockResult::MustWipe), int(r.kind));
}

// ---- Restore ----------------------------------------------------------------

static std::vector<uint8_t> make_backup(const char* pass, int entries) {
    MemoryStore s;
    VaultSession v;
    v.create(s, pass, kFast);
    for (int i = 0; i < entries; i++) {
        Entry e;
        e.type = EntryType::Note;
        e.title = "backup";
        v.data().add(e);
    }
    std::vector<uint8_t> image;
    v.export_image(pass, kFast, image);
    return image;
}

void test_restore_declined_while_locked_does_not_reset_counter() {
    // Regression for the bypass fixed in #24.
    Fixture f;
    for (int i = 0; i < 9; i++) unlock_attempt(f.guard, f.session, f.store, "guess");
    TEST_ASSERT_EQUAL(9, f.guard.failures());
    auto attacker_backup = make_backup("attacker knows this", 0);
    RestoreCheck c = restore_check(f.guard, true, attacker_backup, "attacker knows this");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Ok), int(c.kind));
    // User/attacker declines the restore: counter unchanged.
    TEST_ASSERT_EQUAL(9, f.guard.failures());
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "guess");
    TEST_ASSERT_EQUAL(int(UnlockResult::MustWipe), int(r.kind));
}

void test_oversize_or_bad_layout_costs_no_attempt() {
    Fixture f;
    std::vector<uint8_t> big;
    f.store.read(big);
    big.resize(kMaxFileSize + kPadBlock, 0);
    f.store.blob = big;
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "guess");
    TEST_ASSERT_EQUAL(int(UnlockResult::Damaged), int(r.kind));
    TEST_ASSERT_EQUAL(int(VaultError::TooLarge), int(r.error));
    TEST_ASSERT_EQUAL(0, f.guard.failures());

    auto backup = make_backup("backup pass", 1);
    backup.push_back(0);  // body no longer a multiple of the pad block
    RestoreCheck c = restore_check(f.guard, true, backup, "backup pass");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Failed), int(c.kind));
    TEST_ASSERT_EQUAL(0, f.guard.failures());
    backup.assign(kMaxFileSize + kPadBlock, 0);
    c = restore_check(f.guard, true, backup, "backup pass");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Failed), int(c.kind));
    TEST_ASSERT_EQUAL(int(VaultError::TooLarge), int(c.error));
    TEST_ASSERT_EQUAL(0, f.guard.failures());
}

// ---- Delete ---------------------------------------------------------------------

static Entry make_entry(const char* title, const char* secret) {
    Entry e;
    e.type = EntryType::Password;
    e.title = title;
    e.set(kFieldPassword, secret);
    return e;
}

void test_delete_entry_persists_and_removes() {
    VaultData d;
    uint32_t a = d.add(make_entry("a", "pw-a"));
    uint32_t b = d.add(make_entry("b", "pw-b"));
    int calls = 0;
    TEST_ASSERT_TRUE(delete_entry(d, a, [&] {
        calls++;
        TEST_ASSERT_NULL(d.find(a));  // already gone when persisting
        return true;
    }));
    TEST_ASSERT_EQUAL(1, calls);
    TEST_ASSERT_EQUAL(1, int(d.entries.size()));
    TEST_ASSERT_NOT_NULL(d.find(b));
}

void test_delete_entry_rolls_back_when_save_fails() {
    VaultData d;
    d.add(make_entry("a", "pw-a"));
    uint32_t b = d.add(make_entry("b", "pw-b"));
    d.add(make_entry("c", "pw-c"));
    uint32_t next = d.next_id;
    TEST_ASSERT_FALSE(delete_entry(d, b, [] { return false; }));
    TEST_ASSERT_EQUAL(3, int(d.entries.size()));
    // Same position, same contents, same id counter.
    TEST_ASSERT_EQUAL_UINT32(b, d.entries[1].id);
    TEST_ASSERT_EQUAL_STRING("b", d.entries[1].title.c_str());
    TEST_ASSERT_EQUAL_STRING("pw-b", d.entries[1].get_or(kFieldPassword).c_str());
    TEST_ASSERT_EQUAL_STRING("a", d.entries[0].title.c_str());
    TEST_ASSERT_EQUAL_STRING("c", d.entries[2].title.c_str());
    TEST_ASSERT_EQUAL_UINT32(next, d.next_id);
}

void test_delete_entry_unknown_id_does_not_persist() {
    VaultData d;
    d.add(make_entry("a", "pw-a"));
    bool persisted = false;
    TEST_ASSERT_FALSE(delete_entry(d, 999, [&] {
        persisted = true;
        return true;
    }));
    TEST_ASSERT_FALSE(persisted);
    TEST_ASSERT_EQUAL(1, int(d.entries.size()));
}

// ---- Compact -----------------------------------------------------------------

struct FlakyStore : MemoryStore {
    bool fail_erase = false, fail_write = false;
    int erases = 0;
    bool erase() override {
        erases++;
        return !fail_erase && MemoryStore::erase();
    }
    bool write(const std::vector<uint8_t>& d) override {
        return !fail_write && MemoryStore::write(d);
    }
};

void test_compact_rewrites_current_vault_and_keeps_counter() {
    Fixture f;
    TEST_ASSERT_EQUAL(int(UnlockResult::Unlocked),
                      int(unlock_attempt(f.guard, f.session, f.store, "right horse battery").kind));
    f.guard.begin_attempt();
    f.guard.begin_attempt();
    std::vector<uint8_t> before, copied;
    f.store.read(before);
    bool missing = false;
    auto copy = [&copied](const std::vector<uint8_t>& img) {
        copied = img;
        return true;
    };
    TEST_ASSERT_EQUAL(int(CompactResult::Done),
                      int(compact_vault(f.session, f.store, copy, missing)));
    TEST_ASSERT_FALSE(missing);
    std::vector<uint8_t> after;
    f.store.read(after);
    TEST_ASSERT_TRUE(before != after);  // fresh nonce
    TEST_ASSERT_TRUE(copied == after);  // the safety copy is the exact image written
    TEST_ASSERT_EQUAL(2, f.guard.failures());
    f.session.close();
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(f.session.unlock(f.store, "right horse battery")));
    TEST_ASSERT_EQUAL(1, f.session.data().entries.size());
    // The safety copy restores with the vault passphrase.
    VaultSession from_copy;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(from_copy.unlock_bytes(copied, "right horse battery")));
}

void test_compact_needs_open_session_and_touches_nothing() {
    Fixture f;  // session closed
    std::vector<uint8_t> before = f.store.blob;
    FlakyStore s;
    s.write(before);
    bool missing = false;
    TEST_ASSERT_EQUAL(int(CompactResult::NotOpen), int(compact_vault(f.session, s, {}, missing)));
    TEST_ASSERT_EQUAL(0, s.erases);
    TEST_ASSERT_TRUE(s.blob == before);
}

void test_compact_without_safety_copy_touches_nothing() {
    Fixture f;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(f.session.unlock(f.store, "right horse battery")));
    FlakyStore s;
    s.write(f.store.blob);
    bool missing = false;
    auto no_card = [](const std::vector<uint8_t>&) { return false; };
    TEST_ASSERT_EQUAL(int(CompactResult::NoSafetyCopy),
                      int(compact_vault(f.session, s, no_card, missing)));
    TEST_ASSERT_EQUAL(0, s.erases);
    TEST_ASSERT_FALSE(missing);
    TEST_ASSERT_TRUE(s.blob == f.store.blob);
}

void test_compact_storage_failures_keep_session_open() {
    for (int mode = 0; mode < 2; mode++) {
        Fixture f;
        TEST_ASSERT_EQUAL(int(VaultError::Ok), int(f.session.unlock(f.store, "right horse battery")));
        FlakyStore s;
        s.write(f.store.blob);
        s.fail_erase = mode == 0;
        s.fail_write = mode == 1;
        bool missing = false;
        TEST_ASSERT_EQUAL(int(CompactResult::StorageFailed),
                          int(compact_vault(f.session, s, {}, missing)));
        TEST_ASSERT_TRUE(f.session.is_open());
        TEST_ASSERT_TRUE(missing);
        // Locking must not drop the only copy while writes still fail. (A
        // failed erase with working writes is already recovered by the save.)
        if (mode == 1) {
            TEST_ASSERT_FALSE(persist_before_lock(f.session, s, missing));
            TEST_ASSERT_TRUE(missing);
            TEST_ASSERT_TRUE(f.session.is_open());
        }
        // Once storage works again, locking saves the vault first.
        s.fail_erase = s.fail_write = false;
        TEST_ASSERT_TRUE(persist_before_lock(f.session, s, missing));
        TEST_ASSERT_FALSE(missing);
        VaultSession check;
        TEST_ASSERT_EQUAL(int(VaultError::Ok), int(check.unlock(s, "right horse battery")));
    }
}

void test_persist_before_lock_is_a_no_op_normally() {
    Fixture f;
    TEST_ASSERT_EQUAL(int(VaultError::Ok), int(f.session.unlock(f.store, "right horse battery")));
    FlakyStore s;
    s.fail_write = true;  // would fail if it tried to save
    bool missing = false;
    TEST_ASSERT_TRUE(persist_before_lock(f.session, s, missing));
}

void test_restore_wrong_passphrase_counts_while_locked() {
    Fixture f;
    auto backup = make_backup("backup pass", 2);
    RestoreCheck c = restore_check(f.guard, true, backup, "wrong");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Wrong), int(c.kind));
    TEST_ASSERT_EQUAL(1, f.guard.failures());
    for (int i = 0; i < 9; i++) c = restore_check(f.guard, true, backup, "wrong");
    TEST_ASSERT_EQUAL(int(RestoreCheck::MustWipe), int(c.kind));
}

void test_restore_empty_passphrase_not_counted() {
    Fixture f;
    auto backup = make_backup("backup pass", 1);
    for (int i = 0; i < 12; i++) {
        RestoreCheck c = restore_check(f.guard, true, backup, "");
        TEST_ASSERT_EQUAL(int(RestoreCheck::Empty), int(c.kind));
        c = restore_check(f.guard, false, backup, "");
        TEST_ASSERT_EQUAL(int(RestoreCheck::Empty), int(c.kind));
    }
    TEST_ASSERT_EQUAL(0, f.guard.failures());
    RestoreCheck c = restore_check(f.guard, true, backup, "backup pass");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Ok), int(c.kind));
}

void test_restore_empty_passphrase_with_exhausted_guard_must_wipe() {
    Fixture f;
    auto backup = make_backup("backup pass", 1);
    for (uint32_t i = 0; i < kMaxAttempts; i++) f.guard.begin_attempt();
    RestoreCheck c = restore_check(f.guard, true, backup, "");
    TEST_ASSERT_EQUAL(int(RestoreCheck::MustWipe), int(c.kind));
}

void test_restore_wrong_while_locked_reports_delay() {
    Fixture f;
    auto backup = make_backup("backup pass", 1);
    for (uint32_t i = 1; i <= 5; i++) {
        RestoreCheck c = restore_check(f.guard, true, backup, "wrong");
        TEST_ASSERT_EQUAL(int(RestoreCheck::Wrong), int(c.kind));
        TEST_ASSERT_EQUAL(attempt_delay_ms(i), c.retry_delay_ms);
    }
    RestoreCheck c = restore_check(f.guard, false, backup, "wrong");
    TEST_ASSERT_EQUAL(0, c.retry_delay_ms);
}

void test_restore_while_unlocked_does_not_count() {
    Fixture f;
    auto backup = make_backup("backup pass", 2);
    RestoreCheck c = restore_check(f.guard, false, backup, "wrong");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Wrong), int(c.kind));
    TEST_ASSERT_EQUAL(0, f.guard.failures());
    c = restore_check(f.guard, false, backup, "backup pass");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Ok), int(c.kind));
    TEST_ASSERT_EQUAL(2, c.entries);
}

void test_restore_corrupt_image_refunds() {
    Fixture f;
    auto backup = make_backup("backup pass", 1);
    backup[0] = 'X';
    RestoreCheck c = restore_check(f.guard, true, backup, "backup pass");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Failed), int(c.kind));
    TEST_ASSERT_EQUAL(int(VaultError::BadMagic), int(c.error));
    TEST_ASSERT_EQUAL(0, f.guard.failures());
}

// Vault whose passphrase is right but whose payload is not readable.
static std::vector<uint8_t> make_vault_with_bad_payload(const char* pass, uint8_t version_xor) {
    Header h;
    h.kdf = kFast;
    header_new_salt(h);
    Key32 kek;
    kdf_derive(kek.data(), reinterpret_cast<const uint8_t*>(pass), strlen(pass), h.salt, h.kdf);
    uint8_t dek[32];
    for (int i = 0; i < 32; i++) dek[i] = uint8_t(i + 1);
    header_wrap(h, kek.data(), dek);
    SBytes payload;
    serialize(VaultData(), payload);
    payload[0] ^= version_xor;
    std::vector<uint8_t> out;
    seal_file(h, dek, payload.data(), payload.size(), out);
    return out;
}

void test_unlock_payload_version_error_counts_as_correct_passphrase() {
    Fixture f;
    f.guard.begin_attempt();
    f.guard.begin_attempt();
    f.store.write(make_vault_with_bad_payload("right horse battery", 0x7f));
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "right horse battery");
    TEST_ASSERT_EQUAL(int(UnlockResult::Damaged), int(r.kind));
    TEST_ASSERT_EQUAL(int(VaultError::UnsupportedVersion), int(r.error));
    TEST_ASSERT_TRUE(r.passphrase_ok);
    TEST_ASSERT_FALSE(f.session.is_open());
    TEST_ASSERT_EQUAL(0, f.guard.failures());  // success, not a refund of one attempt
    // A wrong passphrase on the same vault still counts.
    r = unlock_attempt(f.guard, f.session, f.store, "guess");
    TEST_ASSERT_EQUAL(int(UnlockResult::Wrong), int(r.kind));
    TEST_ASSERT_EQUAL(1, f.guard.failures());
}

void test_unlock_unreadable_header_costs_nothing() {
    Fixture f;
    f.store.blob[0] = 'X';  // bad magic: rejected before any KDF runs
    for (int i = 0; i < 12; i++) {
        UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "guess");
        TEST_ASSERT_EQUAL(int(UnlockResult::Damaged), int(r.kind));
        TEST_ASSERT_EQUAL(int(VaultError::BadMagic), int(r.error));
        TEST_ASSERT_FALSE(r.passphrase_ok);
    }
    TEST_ASSERT_EQUAL(0, f.guard.failures());
    f.store.blob.resize(10);
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "guess");
    TEST_ASSERT_EQUAL(int(VaultError::Truncated), int(r.error));
    TEST_ASSERT_EQUAL(0, f.guard.failures());
}

void test_unlock_io_error_never_touches_counter() {
    Fixture f;
    f.kv.fail_writes = true;  // would be Refused if the attempt were begun
    f.store.erase();
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "right horse battery");
    TEST_ASSERT_EQUAL(int(UnlockResult::Failed), int(r.kind));
    TEST_ASSERT_EQUAL(int(VaultError::Io), int(r.error));
}

void test_unlock_with_counter_missing_allows_one_guess() {
    Fixture f;
    f.kv.values.clear();  // NVS wiped while the vault stayed
    TEST_ASSERT_EQUAL(int(kFailuresIfMissing), int(f.guard.failures()));
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "guess");
    TEST_ASSERT_EQUAL(int(UnlockResult::MustWipe), int(r.kind));
    // The one guess may also be the right one, which restores a real counter.
    f.kv.values.clear();
    TEST_ASSERT_EQUAL(int(UnlockResult::Unlocked),
                      int(unlock_attempt(f.guard, f.session, f.store, "right horse battery").kind));
    TEST_ASSERT_EQUAL(0, f.guard.failures());
    TEST_ASSERT_EQUAL(1, f.kv.values.count("fails"));
}

void test_unlock_refused_when_counter_unreadable() {
    Fixture f;
    f.kv.values["fails"] = 9;
    f.kv.fail_reads = true;
    UnlockResult r = unlock_attempt(f.guard, f.session, f.store, "right horse battery");
    TEST_ASSERT_EQUAL(int(UnlockResult::Refused), int(r.kind));
    TEST_ASSERT_FALSE(f.session.is_open());
    f.kv.fail_reads = false;
    TEST_ASSERT_EQUAL(9, f.guard.failures());
}

void test_damaged_text_by_stage_and_error() {
    TEST_ASSERT_EQUAL_STRING(msg::kPayloadDamaged, damaged_text(VaultError::Corrupt, true));
    // After the key unwrapped, any payload error means broken data.
    TEST_ASSERT_EQUAL_STRING(msg::kPayloadDamaged, damaged_text(VaultError::Truncated, true));
    TEST_ASSERT_EQUAL_STRING(msg::kVaultNewer, damaged_text(VaultError::UnsupportedVersion, true));
    TEST_ASSERT_EQUAL_STRING(msg::kVaultNewer, damaged_text(VaultError::UnsupportedVersion, false));
    TEST_ASSERT_EQUAL_STRING(msg::kVaultUnsupported, damaged_text(VaultError::BadParams, false));
    TEST_ASSERT_EQUAL_STRING(msg::kVaultUnreadable, damaged_text(VaultError::BadMagic, false));
    TEST_ASSERT_EQUAL_STRING(msg::kVaultUnreadable, damaged_text(VaultError::Truncated, false));
}

void test_restore_unreadable_image_costs_nothing_even_when_locked() {
    Fixture f;
    f.kv.fail_writes = true;  // would be Refused if an attempt were begun
    auto backup = make_backup("backup pass", 1);
    backup.resize(8);
    RestoreCheck c = restore_check(f.guard, true, backup, "backup pass");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Failed), int(c.kind));
    TEST_ASSERT_EQUAL(int(VaultError::Truncated), int(c.error));
}

void test_restore_backup_with_broken_payload_refunds() {
    Fixture f;
    auto backup = make_vault_with_bad_payload("backup pass", 0x7f);
    RestoreCheck c = restore_check(f.guard, true, backup, "backup pass");
    TEST_ASSERT_EQUAL(int(RestoreCheck::Failed), int(c.kind));
    TEST_ASSERT_EQUAL(int(VaultError::UnsupportedVersion), int(c.error));
    TEST_ASSERT_EQUAL(0, f.guard.failures());  // says nothing about the device passphrase
}

void test_restore_commit_replaces_vault_and_resets_counter() {
    Fixture f;
    for (int i = 0; i < 5; i++) unlock_attempt(f.guard, f.session, f.store, "guess");
    auto backup = make_backup("backup pass", 3);
    TEST_ASSERT_EQUAL(int(VaultError::Ok),
                      int(restore_commit(f.guard, f.session, f.store, backup, "backup pass")));
    TEST_ASSERT_TRUE(f.session.is_open());
    TEST_ASSERT_EQUAL(3, f.session.data().entries.size());
    TEST_ASSERT_EQUAL(0, f.guard.failures());
    f.session.close();
    // The device vault now opens with the backup passphrase only.
    TEST_ASSERT_EQUAL(int(UnlockResult::Wrong),
                      int(unlock_attempt(f.guard, f.session, f.store, "right horse battery").kind));
    TEST_ASSERT_EQUAL(int(UnlockResult::Unlocked),
                      int(unlock_attempt(f.guard, f.session, f.store, "backup pass").kind));
}

void test_restore_commit_io_error_keeps_counter() {
    Fixture f;
    unlock_attempt(f.guard, f.session, f.store, "guess");
    struct FailingStore : MemoryStore {
        bool write(const std::vector<uint8_t>&) override { return false; }
    } bad;
    auto backup = make_backup("backup pass", 1);
    TEST_ASSERT_EQUAL(int(VaultError::Io), int(restore_commit(f.guard, f.session, bad, backup, "backup pass")));
    TEST_ASSERT_EQUAL(1, f.guard.failures());
    TEST_ASSERT_FALSE(f.session.is_open());
}

void test_unlock_leaves_counter_when_reset_fails() {
    Fixture f;
    FlakyKv kv;
    AttemptGuard g(kv);
    g.reset();
    kv.writes_left = 1;  // begin_attempt persists, record_success fails
    TEST_ASSERT_EQUAL(int(UnlockResult::Unlocked),
                      int(unlock_attempt(g, f.session, f.store, "right horse battery").kind));
    // Fails closed: the attempt stays counted, and the UI warns about it.
    TEST_ASSERT_EQUAL(1, g.failures());
    f.session.close();
}

// ---- Locking ----------------------------------------------------------------

void test_lock_destroys_all_screens_and_wipes_session() {
    Fixture f;
    unlock_attempt(f.guard, f.session, f.store, "right horse battery");
    Router r;
    r.push(fake("home"));
    r.push(fake("entry_view"));
    r.push(fake("text_input"));
    r.apply();
    r.push(fake("queued"));
    g_log.clear();
    lock_session(r, f.session, fake("unlock"));
    // All unlocked screens are gone before the unlock screen appears.
    int deletions = 0;
    for (auto& l : g_log) deletions += l.rfind("del ", 0) == 0;
    TEST_ASSERT_EQUAL(4, deletions);
    TEST_ASSERT_FALSE(f.session.is_open());
    TEST_ASSERT_EQUAL(0, f.session.data().entries.size());
    r.apply();
    TEST_ASSERT_EQUAL(1, r.depth());
    TEST_ASSERT_EQUAL_STRING("unlock", top_name(r).c_str());
    // Nothing to go back to.
    r.pop();
    r.apply();
    TEST_ASSERT_EQUAL(0, r.depth());
}

void test_autolock_step() {
    const uint32_t limit = 60000, dim = 10000;
    TEST_ASSERT_EQUAL(int(AutolockAction::None), int(autolock_step(false, false, 999999, limit, dim)));
    TEST_ASSERT_EQUAL(int(AutolockAction::Wake), int(autolock_step(false, true, 0, limit, dim)));
    TEST_ASSERT_EQUAL(int(AutolockAction::None), int(autolock_step(true, false, 49999, limit, dim)));
    TEST_ASSERT_EQUAL(int(AutolockAction::Dim), int(autolock_step(true, false, 50000, limit, dim)));
    TEST_ASSERT_EQUAL(int(AutolockAction::None), int(autolock_step(true, true, 55000, limit, dim)));
    TEST_ASSERT_EQUAL(int(AutolockAction::Lock), int(autolock_step(true, true, 60000, limit, dim)));
    TEST_ASSERT_EQUAL(int(AutolockAction::Lock), int(autolock_step(true, false, 70000, limit, dim)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_router_ops_are_deferred_until_apply);
    RUN_TEST(test_router_push_pop_replace_reset);
    RUN_TEST(test_router_apply_reports_navigation);
    RUN_TEST(test_router_resumes_new_top_and_orders_ops);
    RUN_TEST(test_router_screen_can_pop_itself_inside_handler);
    RUN_TEST(test_router_clear_now_destroys_everything_and_drops_pending);
    RUN_TEST(test_boot_route);
    RUN_TEST(test_boot_route_without_counter_storage_blocks_everything);
    RUN_TEST(test_wipe_success_clears_vault_and_counter);
    RUN_TEST(test_wipe_failure_keeps_counter_exhausted);
    RUN_TEST(test_wipe_reports_counter_that_could_not_be_reset);
    RUN_TEST(test_wipe_runs_even_if_counter_cannot_persist);
    RUN_TEST(test_wipe_not_armed_when_erase_and_counter_both_fail);
    RUN_TEST(test_wipe_armed_by_earlier_failures_even_if_mark_fails);
    RUN_TEST(test_unlock_success_resets_counter);
    RUN_TEST(test_unlock_wrong_counts_and_reports_delay);
    RUN_TEST(test_unlock_counter_persisted_before_check);
    RUN_TEST(test_unlock_refused_when_counter_cannot_be_saved);
    RUN_TEST(test_unlock_damaged_vault);
    RUN_TEST(test_unlock_failure_refunds_attempt);
    RUN_TEST(test_unlock_empty_passphrase_not_counted);
    RUN_TEST(test_unlock_empty_passphrase_with_exhausted_guard_must_wipe);
    RUN_TEST(test_current_passphrase_check_counts_against_guard);
    RUN_TEST(test_restore_declined_while_locked_does_not_reset_counter);
    RUN_TEST(test_restore_wrong_passphrase_counts_while_locked);
    RUN_TEST(test_restore_empty_passphrase_not_counted);
    RUN_TEST(test_restore_empty_passphrase_with_exhausted_guard_must_wipe);
    RUN_TEST(test_restore_wrong_while_locked_reports_delay);
    RUN_TEST(test_restore_while_unlocked_does_not_count);
    RUN_TEST(test_restore_corrupt_image_refunds);
    RUN_TEST(test_unlock_payload_version_error_counts_as_correct_passphrase);
    RUN_TEST(test_unlock_unreadable_header_costs_nothing);
    RUN_TEST(test_unlock_io_error_never_touches_counter);
    RUN_TEST(test_unlock_with_counter_missing_allows_one_guess);
    RUN_TEST(test_unlock_refused_when_counter_unreadable);
    RUN_TEST(test_damaged_text_by_stage_and_error);
    RUN_TEST(test_restore_unreadable_image_costs_nothing_even_when_locked);
    RUN_TEST(test_restore_backup_with_broken_payload_refunds);
    RUN_TEST(test_restore_commit_replaces_vault_and_resets_counter);
    RUN_TEST(test_restore_commit_io_error_keeps_counter);
    RUN_TEST(test_unlock_leaves_counter_when_reset_fails);
    RUN_TEST(test_oversize_or_bad_layout_costs_no_attempt);
    RUN_TEST(test_delete_entry_persists_and_removes);
    RUN_TEST(test_delete_entry_rolls_back_when_save_fails);
    RUN_TEST(test_delete_entry_unknown_id_does_not_persist);
    RUN_TEST(test_compact_rewrites_current_vault_and_keeps_counter);
    RUN_TEST(test_compact_needs_open_session_and_touches_nothing);
    RUN_TEST(test_compact_without_safety_copy_touches_nothing);
    RUN_TEST(test_compact_storage_failures_keep_session_open);
    RUN_TEST(test_persist_before_lock_is_a_no_op_normally);
    RUN_TEST(test_lock_destroys_all_screens_and_wipes_session);
    RUN_TEST(test_autolock_step);
    return UNITY_END();
}

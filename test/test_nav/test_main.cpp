// Navigation (router + routing decisions) and security flows.
#include <unity.h>

#include <functional>
#include <string>
#include <vector>

#include "core/flows.h"

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

// ---- Boot route -------------------------------------------------------------

void test_boot_route() {
    TEST_ASSERT_EQUAL(int(BootRoute::NoPartition), int(boot_route(false, false, false, false)));
    TEST_ASSERT_EQUAL(int(BootRoute::StorageUnreadable), int(boot_route(true, false, false, false)));
    TEST_ASSERT_EQUAL(int(BootRoute::StorageUnreadable), int(boot_route(true, false, true, true)));
    TEST_ASSERT_EQUAL(int(BootRoute::Wipe), int(boot_route(true, true, true, true)));
    TEST_ASSERT_EQUAL(int(BootRoute::Wipe), int(boot_route(true, true, true, false)));
    TEST_ASSERT_EQUAL(int(BootRoute::Unlock), int(boot_route(true, true, false, true)));
    TEST_ASSERT_EQUAL(int(BootRoute::Setup), int(boot_route(true, true, false, false)));
}

// ---- Unlock -----------------------------------------------------------------

static const KdfParams kFast{64, 3};

struct Fixture {
    MemoryStore store;
    MemoryKv kv;
    AttemptGuard guard{kv};
    VaultSession session;
    Fixture() {
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
    RUN_TEST(test_router_resumes_new_top_and_orders_ops);
    RUN_TEST(test_router_screen_can_pop_itself_inside_handler);
    RUN_TEST(test_router_clear_now_destroys_everything_and_drops_pending);
    RUN_TEST(test_boot_route);
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
    RUN_TEST(test_restore_commit_replaces_vault_and_resets_counter);
    RUN_TEST(test_restore_commit_io_error_keeps_counter);
    RUN_TEST(test_lock_destroys_all_screens_and_wipes_session);
    RUN_TEST(test_autolock_step);
    return UNITY_END();
}

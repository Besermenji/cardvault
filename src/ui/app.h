// Global application state shared by all screens.
#pragma once

#include <functional>

#include "platform/device.h"
#include "platform/flash_store.h"
#include "platform/nvs_kv.h"
#include "security/attempt_guard.h"
#include "ui/screen.h"
#include "vault/session.h"

// How long a revealed secret stays visible without being hidden again.
// Only test builds may shorten it: a flag in a release build would leave no
// trace in the binary for the release check to find.
#ifndef CV_REVEAL_MS
#define CV_REVEAL_MS 20000
#elif !defined(CV_TEST_HOOKS)
#error "CV_REVEAL_MS may only be set together with CV_TEST_HOOKS"
#elif CV_REVEAL_MS < 1000
#error "CV_REVEAL_MS must be at least 1000"
#endif

namespace cv {
namespace ui {

constexpr uint32_t kRevealMs = CV_REVEAL_MS;

struct App {
    VaultSession session;
    FlashStore store;
    NvsKv guard_kv{CV_NVS_GUARD};
    AttemptGuard guard{guard_kv};
    AttemptGate gate;
    Settings settings;
    Router router;
    Canvas* canvas = nullptr;

    uint32_t last_input_ms = 0;
    bool dimmed = false;

    // Requests a lock; performed by the main loop after the current event,
    // so a screen is never destroyed while its own handler is running.
    void lock() { lock_pending = true; }
    // Destroys all screens, wipes decrypted data and keys, shows Unlock.
    // Refuses (returns false, with a toast) while the session is the only copy
    // of the vault and saving it again fails: see persist_before_lock.
    bool lock_now();
    // Set when a compact erased flash but could not write the vault back.
    bool flash_missing = false;
    bool lock_pending = false;
    // Re-encrypts and writes the vault; shows a toast on failure.
    bool save();
    // Erases the vault partition and the attempt counter, then reboots.
    [[noreturn]] void wipe_and_reboot(const char* reason);
    // Draws a modal "working" message immediately (for blocking operations).
    void busy(const char* msg);
    // Transient notification drawn over the current screen.
    void toast(const char* msg, uint16_t col = color::good, uint32_t ms = 1800);
    void draw_toast(Canvas& c);
    bool toast_active() const;
    void apply_brightness();
    // Starts the delay the guard requires before the next passphrase check.
    void arm_attempt_delay();
    uint32_t attempt_wait_ms() const;

    SString toast_msg;
    uint16_t toast_color = color::good;
    uint32_t toast_until = 0;
};

App& app();

// Screen factories --------------------------------------------------------

struct InputOpts {
    bool mask = false;
    bool multiline = false;
    bool non_blank = false;  // refuse an empty or whitespace-only value
    // Esc asks before dropping text that differs from what the input started
    // with. Off only for prompts whose text is worthless once dropped
    // (passphrase prompts, typing ERASE).
    bool confirm_discard = true;
    bool strength = false;   // show a passphrase strength meter
    size_t min_len = 0;
    size_t max_len = 4000;
    const char* hint = nullptr;
};

using TextDone = std::function<void(SString&)>;

struct MenuItem {
    SString label;
    std::function<void()> action;
    SString detail;  // optional right-aligned text
    uint16_t color = color::text;
    bool destructive = false;  // digit hotkey only selects it; ENTER runs it
};

ScreenPtr make_setup();
ScreenPtr make_unlock();
ScreenPtr make_home();
ScreenPtr make_entry_view(uint32_t id);
ScreenPtr make_entry_edit(uint32_t id, EntryType type, const SString* prefill = nullptr);
ScreenPtr make_text_input(const char* title, const SString& initial, InputOpts opts,
                          TextDone done);
ScreenPtr make_menu(const char* title, std::vector<MenuItem> items);
ScreenPtr make_confirm(const char* title, const char* msg, std::function<void()> yes,
                       uint16_t accent = color::warn);
ScreenPtr make_message(const char* title, const char* msg, uint16_t accent = color::info,
                       std::function<void()> ok = nullptr);
ScreenPtr make_mnemonic_input(int words, const SString& initial, TextDone done);
ScreenPtr make_seed_view(uint32_t id);
ScreenPtr make_text_view(uint32_t id, uint8_t field);
ScreenPtr make_pwgen(TextDone use);
ScreenPtr make_totp_list();
ScreenPtr make_settings();

// Flows -------------------------------------------------------------------

// Asks for a new passphrase (with strength meter) twice; calls done on match.
void ask_new_passphrase(const char* title, TextDone done);
// Input options for typing an existing passphrase (current or backup).
// Empty input is refused before any KDF runs or any attempt is counted.
InputOpts existing_passphrase_opts(const char* hint);
// True (after showing a toast) while the attempt guard's delay is running.
bool attempt_delay_active();
// Times this device and picks the Argon2 parameters for a ~2 s unlock, with at
// least min_mem_kib memory. On failure shows a message and returns false.
bool tune_security(KdfTuning& out, uint32_t min_mem_kib = 0);
// Closed loop for VaultSession::create / change_passphrase: rescales `t` once
// if the real derive misses the target.
KdfCheck kdf_closed_loop(KdfTuning& t);
// If the tuning could not meet the target, says how long an unlock takes.
void show_tune_warning(const KdfTuning& t);
// Opens the clock-setting input; calls done after a valid time was set.
void ask_set_time(std::function<void()> done = nullptr);
// Picks a backup on the SD card and restores it (works while locked).
void start_restore_flow();
void start_backup_flow();
// Menu for choosing a new entry type.
void open_new_entry_menu();
// Seed phrase entry: choose word count / generate, then type words.
void start_mnemonic_flow(const SString& current, TextDone done);

}  // namespace ui
}  // namespace cv

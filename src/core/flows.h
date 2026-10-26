// Navigation and security decisions, separated from drawing so they can be
// unit tested on the host. Screens call these and only render the outcome.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "security/attempt_guard.h"
#include "ui/router.h"
#include "vault/session.h"

namespace cv {

// ---- Boot -----------------------------------------------------------------

enum class BootRoute {
    NoPartition,        // install has no "vault" partition
    GuardUnavailable,   // attempt counter storage (NVS) cannot be opened
    StorageUnreadable,  // partition holds data we cannot mount; ask before formatting
    Wipe,               // attempt limit reached before a wipe completed (power cut)
    Unlock,
    Setup,
};

// Without a working counter nothing else may run: every attempt would be
// unguarded. After that an exhausted counter wins over everything: an
// interrupted wipe may have left the partition unmountable, and the wipe
// must finish before any prompt to format or set up.
BootRoute boot_route(bool has_partition, bool guard_ok, bool mounted, bool guard_exhausted,
                     bool vault_exists);

// ---- Wipe -------------------------------------------------------------------

struct WipeResult {
    bool armed = false;   // counter was exhausted before the erase: boot retries the wipe
    bool erased = false;  // vault data destroyed
    bool reset = false;   // counter back to 0 afterwards
};

// Erases the vault. The counter is first persisted as exhausted so that a
// power cut or failed erase is finished on the next boot, and is reset only
// after the erase succeeded. Persisting the counter is best effort: the erase
// runs either way, and `armed` tells the caller whether a retry is scheduled.
WipeResult wipe_vault(BlobStore& store, AttemptGuard& guard);

// ---- Unlock ---------------------------------------------------------------

struct UnlockResult {
    enum Kind {
        Unlocked,
        Wrong,     // wrong passphrase, more attempts left
        MustWipe,  // attempt limit reached: caller must erase the vault
        Damaged,   // vault unreadable, or passphrase correct but data broken (see error)
        Failed,    // could not finish; out of memory / I/O refunded (or, with
                   // passphrase_ok, counted as success), anything else counted
        Refused,   // counter could not be persisted: attempt not made
        Empty,     // empty passphrase: not checked, not counted
    } kind;
    VaultError error = VaultError::Ok;
    uint32_t retry_delay_ms = 0;
    bool passphrase_ok = false;  // data key unwrapped (set even when a later step failed)
};

// Explains an UnlockResult::Damaged outcome: what broke depends on `error`
// and on whether the passphrase was verified first (payload) or not (header).
const char* damaged_text(VaultError error, bool passphrase_ok);

// Checks `pass` against the vault in `store`, counting the attempt. Also used
// to confirm the current passphrase while unlocked (with a scratch session),
// so that prompt is no unlimited guessing channel either.
UnlockResult unlock_attempt(AttemptGuard& guard, VaultSession& session, BlobStore& store,
                            const SString& pass);

// ---- Restore from backup ----------------------------------------------------

struct RestoreCheck {
    enum Kind {
        Ok,
        Wrong,     // wrong passphrase (counted only while locked)
        MustWipe,  // attempt limit reached: caller must erase the vault
        Failed,    // no verdict on the passphrase (bad image, out of memory, backup
                   // damaged after its key unwrapped): not counted
        Refused,   // counter could not be persisted: attempt not made
        Empty,     // empty passphrase: not checked, not counted
    } kind;
    VaultError error = VaultError::Ok;
    size_t entries = 0;
    uint32_t retry_delay_ms = 0;  // set for a counted failure while locked
};

// Decrypts a backup image to verify the passphrase. While the device is
// locked and holds a vault, attempts count against the guard like the unlock
// screen; a correct backup passphrase only refunds its own attempt (it says
// nothing about the device passphrase). A structurally invalid image and an
// empty passphrase are rejected before any KDF or counting; an exhausted
// guard still yields MustWipe.
RestoreCheck restore_check(AttemptGuard& guard, bool device_locked,
                           const std::vector<uint8_t>& image, const SString& pass);

// Replaces the device vault with the image and opens it. Resets the guard
// only after the old vault has been overwritten.
VaultError restore_commit(AttemptGuard& guard, VaultSession& session, BlobStore& store,
                          const std::vector<uint8_t>& image, const SString& pass);

// ---- Compact ----------------------------------------------------------------

// Flash keeps old ciphertext after every save (copy-on-write): earlier
// versions, deleted entries and, after a passphrase change or restore, a
// header that an old passphrase still opens. Compacting erases the whole
// partition and writes the open vault back, so only the current image remains.
enum class CompactResult {
    Done,
    NotOpen,        // nothing in memory to write back; storage untouched
    SealFailed,     // could not build the image; storage untouched
    NoSafetyCopy,   // the safety copy could not be stored; storage untouched
    StorageFailed,  // erase or write failed: flash may no longer hold the vault,
                    // which lives only in this session until a save succeeds
};

// Stores the exact image somewhere durable (an SD backup) before flash is
// erased, so a power cut during the erase cannot lose the vault.
using SafetyCopy = std::function<bool(const std::vector<uint8_t>& image)>;

// Never touches the attempt counter. `safety_copy` runs first and must
// succeed; pass none only to retry after StorageFailed, when a copy was
// already taken. On StorageFailed the session stays open and `flash_missing`
// is set: see persist_before_lock.
CompactResult compact_vault(const VaultSession& session, BlobStore& store,
                            const SafetyCopy& safety_copy, bool& flash_missing);

// Called before anything closes the session (manual lock, auto-lock). While
// `flash_missing` is set the open session is the only copy of the vault: it
// tries to save it again and returns false (do not lock) if that fails.
bool persist_before_lock(VaultSession& session, BlobStore& store, bool& flash_missing);

// ---- Locking ----------------------------------------------------------------

// Destroys every screen (and any secrets they hold), wipes the session and
// leaves only `unlock_screen` on the stack.
void lock_session(ui::Router& router, VaultSession& session, ui::ScreenPtr unlock_screen);

enum class AutolockAction { None, Dim, Lock, Wake };

AutolockAction autolock_step(bool unlocked, bool dimmed, uint32_t idle_ms, uint32_t limit_ms,
                             uint32_t dim_before_ms);

}  // namespace cv

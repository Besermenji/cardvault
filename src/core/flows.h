// Navigation and security decisions, separated from drawing so they can be
// unit tested on the host. Screens call these and only render the outcome.
#pragma once

#include <cstdint>
#include <vector>

#include "security/attempt_guard.h"
#include "ui/router.h"
#include "vault/session.h"

namespace cv {

// ---- Boot -----------------------------------------------------------------

enum class BootRoute {
    NoPartition,        // install has no "vault" partition
    StorageUnreadable,  // partition holds data we cannot mount; ask before formatting
    Wipe,               // attempt limit reached before a wipe completed (power cut)
    Unlock,
    Setup,
};

BootRoute boot_route(bool has_partition, bool mounted, bool guard_exhausted, bool vault_exists);

// ---- Unlock ---------------------------------------------------------------

struct UnlockResult {
    enum Kind {
        Unlocked,
        Wrong,     // wrong passphrase, more attempts left
        MustWipe,  // attempt limit reached: caller must erase the vault
        Damaged,   // passphrase correct but vault data corrupted
        Failed,    // could not check (out of memory, I/O): attempt refunded
        Refused,   // counter could not be persisted: attempt not made
    } kind;
    VaultError error = VaultError::Ok;
    uint32_t retry_delay_ms = 0;
};

UnlockResult unlock_attempt(AttemptGuard& guard, VaultSession& session, BlobStore& store,
                            const SString& pass);

// ---- Restore from backup ----------------------------------------------------

struct RestoreCheck {
    enum Kind { Ok, Wrong, MustWipe, Failed, Refused } kind;
    VaultError error = VaultError::Ok;
    size_t entries = 0;
};

// Decrypts a backup image to verify the passphrase. While the device is
// locked, attempts count against the guard like the unlock screen; a correct
// backup passphrase only refunds its own attempt (it says nothing about the
// device passphrase). An empty passphrase is rejected without running the KDF
// or counting an attempt.
RestoreCheck restore_check(AttemptGuard& guard, bool device_locked,
                           const std::vector<uint8_t>& image, const SString& pass);

// Replaces the device vault with the image and opens it. Resets the guard
// only after the old vault has been overwritten.
VaultError restore_commit(AttemptGuard& guard, VaultSession& session, BlobStore& store,
                          const std::vector<uint8_t>& image, const SString& pass);

// ---- Locking ----------------------------------------------------------------

// Destroys every screen (and any secrets they hold), wipes the session and
// leaves only `unlock_screen` on the stack.
void lock_session(ui::Router& router, VaultSession& session, ui::ScreenPtr unlock_screen);

enum class AutolockAction { None, Dim, Lock, Wake };

AutolockAction autolock_step(bool unlocked, bool dimmed, uint32_t idle_ms, uint32_t limit_ms,
                             uint32_t dim_before_ms);

}  // namespace cv

#include "core/flows.h"

#include "core/messages.h"

namespace cv {

BootRoute boot_route(bool has_partition, bool guard_ok, bool mounted, bool guard_exhausted,
                     bool vault_exists) {
    if (!has_partition) return BootRoute::NoPartition;
    if (!guard_ok) return BootRoute::GuardUnavailable;
    if (guard_exhausted) return BootRoute::Wipe;
    if (!mounted) return BootRoute::StorageUnreadable;
    return vault_exists ? BootRoute::Unlock : BootRoute::Setup;
}

WipeResult wipe_vault(BlobStore& store, AttemptGuard& guard) {
    WipeResult r;
    guard.mark_exhausted();  // best effort: the erase must run even if NVS fails
    r.armed = guard.exhausted();
    r.erased = store.erase();
    if (r.erased) r.reset = guard.reset();
    return r;
}

namespace {

// Only these say nothing about the passphrase, so only these give the attempt
// back. Every other error after the KDF ran stays counted: a new error path
// must never become a free guess.
bool refundable(VaultError e) { return e == VaultError::OutOfMemory || e == VaultError::Io; }

}  // namespace

const char* damaged_text(VaultError error, bool passphrase_ok) {
    switch (error) {
        case VaultError::UnsupportedVersion: return msg::kVaultNewer;
        case VaultError::BadParams: return msg::kVaultUnsupported;
        default: return passphrase_ok ? msg::kPayloadDamaged : msg::kVaultUnreadable;
    }
}

UnlockResult unlock_attempt(AttemptGuard& guard, VaultSession& session, BlobStore& store,
                            const SString& pass) {
    UnlockResult r{UnlockResult::Refused};
    if (guard.exhausted()) {
        r.kind = UnlockResult::MustWipe;
        return r;
    }
    if (pass.empty()) {
        r.kind = UnlockResult::Empty;
        return r;
    }
    // Reading the file and its header needs no passphrase, so a failure here
    // is not a guess and costs nothing.
    std::vector<uint8_t> file;
    if (!store.read(file)) {
        r.kind = UnlockResult::Failed;
        r.error = VaultError::Io;
        return r;
    }
    Header hdr;
    r.error = VaultSession::peek_header(file, hdr);
    if (r.error != VaultError::Ok) {
        r.kind = UnlockResult::Damaged;
        return r;
    }
    if (!guard.begin_attempt()) return r;  // Refused
    r.error = session.unlock_bytes(file, pass, &r.passphrase_ok);
    if (r.error == VaultError::Ok) {
        guard.record_success();
        r.kind = UnlockResult::Unlocked;
    } else if (r.passphrase_ok) {
        // The data key unwrapped, so the passphrase was right. Out of memory
        // means "try again"; anything else means the payload is broken.
        guard.record_success();
        session.close();
        r.kind = refundable(r.error) ? UnlockResult::Failed : UnlockResult::Damaged;
    } else if (refundable(r.error)) {
        guard.refund();
        r.kind = UnlockResult::Failed;
    } else if (guard.exhausted()) {
        r.kind = UnlockResult::MustWipe;
    } else {
        r.kind = r.error == VaultError::WrongPassphrase ? UnlockResult::Wrong : UnlockResult::Failed;
        r.retry_delay_ms = guard.required_delay_ms();
    }
    return r;
}

RestoreCheck restore_check(AttemptGuard& guard, bool device_locked,
                           const std::vector<uint8_t>& image, const SString& pass) {
    RestoreCheck r{RestoreCheck::Refused};
    if (device_locked && guard.exhausted()) {
        r.kind = RestoreCheck::MustWipe;
        return r;
    }
    if (pass.empty()) {
        r.kind = RestoreCheck::Empty;
        return r;
    }
    Header hdr;
    r.error = VaultSession::peek_header(image, hdr);
    if (r.error != VaultError::Ok) {  // bad image: no KDF, not a guess
        r.kind = RestoreCheck::Failed;
        return r;
    }
    if (device_locked && !guard.begin_attempt()) return r;  // Refused
    VaultSession tmp;
    bool key_ok = false;
    r.error = tmp.unlock_bytes(image, pass, &key_ok);
    r.entries = tmp.data().entries.size();
    tmp.close();
    // A correct backup passphrase says nothing about the device passphrase,
    // and neither do storage or memory errors: those give the attempt back.
    if (r.error == VaultError::Ok || key_ok || refundable(r.error)) {
        if (device_locked) guard.refund();
        r.kind = r.error == VaultError::Ok ? RestoreCheck::Ok : RestoreCheck::Failed;
        return r;
    }
    if (device_locked && guard.exhausted()) {
        r.kind = RestoreCheck::MustWipe;
        return r;
    }
    r.kind = r.error == VaultError::WrongPassphrase ? RestoreCheck::Wrong : RestoreCheck::Failed;
    if (device_locked) r.retry_delay_ms = guard.required_delay_ms();
    return r;
}

VaultError restore_commit(AttemptGuard& guard, VaultSession& session, BlobStore& store,
                          const std::vector<uint8_t>& image, const SString& pass) {
    if (!store.write(image)) return VaultError::Io;
    VaultError e = session.unlock_bytes(image, pass);
    if (e != VaultError::Ok) return e;
    guard.reset();
    return VaultError::Ok;
}

CompactResult compact_vault(const VaultSession& session, BlobStore& store) {
    if (!session.is_open()) return CompactResult::NotOpen;
    std::vector<uint8_t> image;
    if (session.seal(image) != VaultError::Ok) return CompactResult::SealFailed;
    if (!store.erase() || !store.write(image)) return CompactResult::StorageFailed;
    std::vector<uint8_t> check;
    if (!store.read(check) || check != image) return CompactResult::StorageFailed;
    return CompactResult::Done;
}

void lock_session(ui::Router& router, VaultSession& session, ui::ScreenPtr unlock_screen) {
    router.clear_now();
    session.close();
    router.reset(std::move(unlock_screen));
}

AutolockAction autolock_step(bool unlocked, bool dimmed, uint32_t idle_ms, uint32_t limit_ms,
                             uint32_t dim_before_ms) {
    if (!unlocked) return dimmed ? AutolockAction::Wake : AutolockAction::None;
    if (idle_ms >= limit_ms) return AutolockAction::Lock;
    if (!dimmed && idle_ms + dim_before_ms >= limit_ms) return AutolockAction::Dim;
    return AutolockAction::None;
}

}  // namespace cv

#include "core/flows.h"

namespace cv {

BootRoute boot_route(bool has_partition, bool mounted, bool guard_exhausted, bool vault_exists) {
    if (!has_partition) return BootRoute::NoPartition;
    if (!mounted) return BootRoute::StorageUnreadable;
    if (guard_exhausted) return BootRoute::Wipe;
    return vault_exists ? BootRoute::Unlock : BootRoute::Setup;
}

UnlockResult unlock_attempt(AttemptGuard& guard, VaultSession& session, BlobStore& store,
                            const SString& pass) {
    UnlockResult r{UnlockResult::Refused};
    if (!guard.begin_attempt()) {
        r.kind = guard.exhausted() ? UnlockResult::MustWipe : UnlockResult::Refused;
        return r;
    }
    r.error = session.unlock(store, pass);
    switch (r.error) {
        case VaultError::Ok:
            guard.record_success();
            r.kind = UnlockResult::Unlocked;
            break;
        case VaultError::WrongPassphrase:
            if (guard.exhausted()) {
                r.kind = UnlockResult::MustWipe;
            } else {
                r.kind = UnlockResult::Wrong;
                r.retry_delay_ms = guard.required_delay_ms();
            }
            break;
        case VaultError::Corrupt:
            // The data key unwrapped, so the passphrase was right.
            guard.record_success();
            session.close();
            r.kind = UnlockResult::Damaged;
            break;
        default:
            guard.refund();
            r.kind = UnlockResult::Failed;
            break;
    }
    return r;
}

RestoreCheck restore_check(AttemptGuard& guard, bool device_locked,
                           const std::vector<uint8_t>& image, const SString& pass) {
    RestoreCheck r{RestoreCheck::Refused};
    if (device_locked && !guard.begin_attempt()) {
        r.kind = guard.exhausted() ? RestoreCheck::MustWipe : RestoreCheck::Refused;
        return r;
    }
    VaultSession tmp;
    r.error = tmp.unlock_bytes(image, pass);
    r.entries = tmp.data().entries.size();
    tmp.close();
    if (r.error == VaultError::Ok) {
        if (device_locked) guard.refund();
        r.kind = RestoreCheck::Ok;
        return r;
    }
    if (r.error != VaultError::WrongPassphrase) {
        if (device_locked) guard.refund();
        r.kind = RestoreCheck::Failed;
        return r;
    }
    r.kind = (device_locked && guard.exhausted()) ? RestoreCheck::MustWipe : RestoreCheck::Wrong;
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

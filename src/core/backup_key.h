// A saved Password entry used as the passphrase of an SD backup (#84).
#pragma once

#include <cstdint>
#include <vector>

#include "vault/model.h"

namespace cv {

struct BackupKeyChoice {
    uint32_t id = 0;
    SString title;
    bool usable = false;  // meets the backup passphrase policy (check_new_passphrase)
};

// Every Password entry, in home-list order (entry_title_less). Entries whose
// password breaks the policy are listed too, with usable == false, so the
// user sees why one is missing rather than not finding it.
std::vector<BackupKeyChoice> backup_key_choices(const VaultData& data);

enum class BackupKey {
    Ok,
    Missing,  // no entry with this id, or it is not a Password entry
    Weak,     // breaks the policy for the purpose (a new backup: 8-128 chars; restore: non-empty)
};

// Copies the entry's password into `out` (wiped on destruction). A new backup
// (`for_backup`) needs check_new_passphrase; a restore only a non-empty value,
// since restore_check() decides whether it opens the file. `out` is cleared
// unless the result is Ok.
BackupKey backup_key_from_entry(const VaultData& data, uint32_t id, bool for_backup, SString& out);

}  // namespace cv

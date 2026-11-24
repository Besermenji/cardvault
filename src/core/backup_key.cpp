#include "core/backup_key.h"

#include <algorithm>

#include "core/entry_rules.h"
#include "vault/format.h"

namespace cv {

namespace {

const Entry* find_password_entry(const VaultData& data, uint32_t id) {
    for (const auto& e : data.entries)
        if (e.id == id) return e.type == EntryType::Password ? &e : nullptr;
    return nullptr;
}

}  // namespace

std::vector<BackupKeyChoice> backup_key_choices(const VaultData& data) {
    std::vector<const Entry*> list;
    for (const auto& e : data.entries)
        if (e.type == EntryType::Password) list.push_back(&e);
    std::sort(list.begin(), list.end(),
              [](const Entry* a, const Entry* b) { return entry_title_less(*a, *b); });
    std::vector<BackupKeyChoice> out;
    out.reserve(list.size());
    for (const Entry* e : list) {
        BackupKeyChoice c;
        c.id = e->id;
        c.title = e->title;
        c.usable = check_new_passphrase(e->value(kFieldPassword)) == VaultError::Ok;
        out.push_back(std::move(c));
    }
    return out;
}

BackupKey backup_key_from_entry(const VaultData& data, uint32_t id, bool for_backup, SString& out) {
    wipe(out);
    out.clear();
    const Entry* e = find_password_entry(data, id);
    if (!e) return BackupKey::Missing;
    const SString& pass = e->value(kFieldPassword);
    bool ok = for_backup ? check_new_passphrase(pass) == VaultError::Ok : !pass.empty();
    if (!ok) return BackupKey::Weak;
    out = pass;
    return BackupKey::Ok;
}

}  // namespace cv

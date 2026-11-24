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

bool blank(const SString& s) {
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
    return true;
}

}  // namespace

BackupKeyProblem backup_key_problem(const SString& pass) {
    if (pass.size() < kMinPassphraseLen) return BackupKeyProblem::Short;
    if (pass.size() > kMaxPassphraseLen) return BackupKeyProblem::Long;
    return blank(pass) ? BackupKeyProblem::Blank : BackupKeyProblem::None;
}

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
        c.problem = backup_key_problem(e->value(kFieldPassword));
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
    bool ok = for_backup ? backup_key_problem(pass) == BackupKeyProblem::None : !pass.empty();
    if (!ok) return BackupKey::Weak;
    out = pass;
    return BackupKey::Ok;
}

}  // namespace cv

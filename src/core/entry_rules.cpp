#include "core/entry_rules.h"

#include "features/base32.h"

namespace cv {

const char* finalize_entry(Entry& draft) {
    if (draft.type == EntryType::Totp) {
        SBytes key;
        if (!base32_decode(draft.get_or(kFieldTotpSecret), key) || key.empty())
            return "Secret is not valid base32";
    }
    if (draft.title.empty()) {
        SString issuer = draft.get_or(kFieldIssuer);
        if (draft.type == EntryType::Totp && !issuer.empty())
            draft.title = issuer;
        else
            draft.title = entry_type_name(draft.type);
        wipe(issuer);
    }
    return nullptr;
}

}  // namespace cv

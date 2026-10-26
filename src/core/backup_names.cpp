#include "core/backup_names.h"

#include <algorithm>
#include <cstdio>

namespace cv {

namespace {
const char kPrefix[] = "backup-";
const char kSuffix[] = ".cvb";
constexpr size_t kPrefixLen = sizeof kPrefix - 1;
constexpr size_t kSuffixLen = sizeof kSuffix - 1;
constexpr size_t kMaxDigits = 8;
constexpr long kMaxNumber = 99999999;
}  // namespace

long backup_number(const std::string& name) {
    if (name.size() < kPrefixLen + 1 + kSuffixLen) return -1;
    if (name.compare(0, kPrefixLen, kPrefix) != 0) return -1;
    if (name.compare(name.size() - kSuffixLen, kSuffixLen, kSuffix) != 0) return -1;
    size_t digits = name.size() - kPrefixLen - kSuffixLen;
    if (digits > kMaxDigits) return -1;
    long v = 0;
    for (size_t i = 0; i < digits; i++) {
        char c = name[kPrefixLen + i];
        if (c < '0' || c > '9') return -1;
        v = v * 10 + (c - '0');
    }
    return v;
}

std::string backup_name(long number) {
    char buf[32];
    snprintf(buf, sizeof buf, "backup-%04ld.cvb", number);
    return buf;
}

void sort_backups(std::vector<std::string>& names) {
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        long na = backup_number(a), nb = backup_number(b);
        if ((na >= 0) != (nb >= 0)) return na >= 0;  // numbered first
        if (na >= 0 && na != nb) return na > nb;
        return na >= 0 ? a > b : a < b;  // same number, different padding / others A-Z
    });
}

long next_backup_number(const std::vector<std::string>& existing) {
    long next = 1;
    for (auto& n : existing) {
        long v = backup_number(n);
        if (v >= next) next = v + 1;
    }
    return next > kMaxNumber ? -1 : next;
}

}  // namespace cv

// Naming and ordering of SD backup files (backup-0001.cvb, ...). Pure string
// logic so it can be unit tested on the host.
#pragma once

#include <string>
#include <vector>

namespace cv {

// "backup-<1-8 digits>.cvb" -> the number, anything else -> -1.
long backup_number(const std::string& name);
// "backup-0007.cvb"; numbers past 9999 simply grow wider.
std::string backup_name(long number);
// Newest first: numbered backups by descending number (numeric, not
// lexicographic), then any other .cvb files alphabetically.
void sort_backups(std::vector<std::string>& names);
// One above the highest number in use; 1 when none. Returns -1 when the
// numbering space is used up.
long next_backup_number(const std::vector<std::string>& existing);

}  // namespace cv

// UTC date/time parsing and formatting (no libc timezone dependencies).
#pragma once

#include <cstdint>

namespace cv {

int64_t days_from_civil(int y, unsigned m, unsigned d);
// Accepts "YYYY-MM-DD HH:MM" or "YYYY-MM-DD HH:MM:SS".
bool parse_utc(const char* s, uint32_t& out);
// Writes "YYYY-MM-DD HH:MM:SS" (buf must hold 20 bytes).
void format_utc(uint32_t t, char* buf);

}  // namespace cv

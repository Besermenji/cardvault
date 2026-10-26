// UTC date/time parsing and formatting (no libc timezone dependencies).
#pragma once

#include <cstdint>

namespace cv {

// Earliest time the device accepts as a set clock (2025-01-01 00:00:00 UTC);
// anything before it means "not set".
constexpr uint32_t kClockFloor = 1735689600;

int64_t days_from_civil(int y, unsigned m, unsigned d);
// Accepts exactly "YYYY-MM-DD HH:MM" or "YYYY-MM-DD HH:MM:SS": fixed-width
// digits, no signs, spaces or trailing text. Does not apply kClockFloor.
bool parse_utc(const char* s, uint32_t& out);
// Writes "YYYY-MM-DD HH:MM:SS" (buf must hold 20 bytes).
void format_utc(uint32_t t, char* buf);

}  // namespace cv

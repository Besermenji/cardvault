#include "features/timeutil.h"

#include <cstdio>

namespace cv {

// Howard Hinnant's algorithm.
int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

bool parse_utc(const char* s, uint32_t& out) {
    int y, mo, d, h, mi, sec = 0;
    char tail;
    int n = sscanf(s, "%d-%d-%d %d:%d:%d%c", &y, &mo, &d, &h, &mi, &sec, &tail);
    if (n != 5 && n != 6) return false;
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1970 || y > 2105 || mo < 1 || mo > 12 || d < 1) return false;
    int dim = mdays[mo - 1] + (mo == 2 && leap(y) ? 1 : 0);
    if (d > dim || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59) return false;
    int64_t t = days_from_civil(y, unsigned(mo), unsigned(d)) * 86400 + h * 3600 + mi * 60 + sec;
    out = uint32_t(t);
    return true;
}

void format_utc(uint32_t t, char* buf) {
    int64_t z = t / 86400;
    uint32_t secs = t % 86400;
    z += 719468;
    const int64_t era = z / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = int(yoe + era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    snprintf(buf, 20, "%04d-%02u-%02u %02u:%02u:%02u", y, m, d, secs / 3600, (secs / 60) % 60,
             secs % 60);
}

}  // namespace cv

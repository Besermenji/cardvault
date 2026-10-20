#include "features/base32.h"

namespace cv {

bool base32_decode(const SString& in, SBytes& out) {
    wipe(out);
    uint32_t buffer = 0;
    int bits = 0;
    bool padding = false;
    for (char c : in) {
        if (c == ' ' || c == '-') continue;
        if (c == '=') {
            padding = true;
            continue;
        }
        if (padding) return false;  // data after padding
        int v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a';
        else if (c >= '2' && c <= '7')
            v = c - '2' + 26;
        else
            return false;
        buffer = (buffer << 5) | uint32_t(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t(buffer >> bits));
        }
    }
    buffer = 0;
    return true;
}

}  // namespace cv

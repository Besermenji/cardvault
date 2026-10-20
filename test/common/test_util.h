#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

inline std::vector<uint8_t> from_hex(const char* hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
        unsigned v;
        sscanf(hex + i, "%2x", &v);
        out.push_back(uint8_t(v));
    }
    return out;
}

inline std::string to_hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

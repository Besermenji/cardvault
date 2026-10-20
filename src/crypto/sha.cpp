#include "crypto/sha.h"

#include <cstring>

#include "crypto/secure.h"

namespace cv {

namespace {

inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

inline uint32_t load_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

// Shared Merkle–Damgård driver: pads and feeds 64-byte blocks to `compress`.
template <typename Compress>
void md_hash(const uint8_t* msg, size_t len, Compress compress) {
    uint8_t block[64];
    size_t full = len / 64;
    for (size_t i = 0; i < full; i++) compress(msg + i * 64);
    size_t rem = len % 64;
    memset(block, 0, 64);
    memcpy(block, msg + full * 64, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        compress(block);
        memset(block, 0, 64);
    }
    uint64_t bits = uint64_t(len) * 8;
    for (int i = 0; i < 8; i++) block[63 - i] = uint8_t(bits >> (8 * i));
    compress(block);
    wipe(block, sizeof block);
}

const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

}  // namespace

void sha1(const uint8_t* msg, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    md_hash(msg, len, [&](const uint8_t* b) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = load_be32(b + 4 * i);
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (bb & c) | (~bb & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = bb ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (bb & c) | (bb & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = bb ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(bb, 30);
            bb = a;
            a = t;
        }
        h[0] += a;
        h[1] += bb;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        wipe(w, sizeof w);
    });
    for (int i = 0; i < 5; i++) store_be32(out + 4 * i, h[i]);
    wipe(h, sizeof h);
}

void sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    md_hash(msg, len, [&](const uint8_t* b) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = load_be32(b + 4 * i);
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6],
                 hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
            uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            uint32_t mj = (a & bb) ^ (a & c) ^ (bb & c);
            uint32_t t2 = S0 + mj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = bb;
            bb = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += bb;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
        wipe(w, sizeof w);
    });
    for (int i = 0; i < 8; i++) store_be32(out + 4 * i, h[i]);
    wipe(h, sizeof h);
}

void hmac_sha1(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
               uint8_t out[20]) {
    uint8_t k[64] = {0};
    if (key_len > 64) {
        sha1(key, key_len, k);
    } else {
        memcpy(k, key, key_len);
    }
    SBytes buf(64 + (msg_len > 20 ? msg_len : 20));
    for (int i = 0; i < 64; i++) buf[i] = k[i] ^ 0x36;
    memcpy(buf.data() + 64, msg, msg_len);
    uint8_t inner[20];
    sha1(buf.data(), 64 + msg_len, inner);
    for (int i = 0; i < 64; i++) buf[i] = k[i] ^ 0x5c;
    memcpy(buf.data() + 64, inner, 20);
    sha1(buf.data(), 64 + 20, out);
    wipe(k, sizeof k);
    wipe(inner, sizeof inner);
}

}  // namespace cv

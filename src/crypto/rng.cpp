#include "crypto/rng.h"

#include <cstring>

#include "crypto/secure.h"

namespace cv {

namespace {
uint8_t g_pool[64];
uint64_t g_counter = 0;

void ratchet() {
    uint8_t msg[8 + 7];
    memcpy(msg, &g_counter, 8);
    memcpy(msg + 8, "ratchet", 7);
    crypto_blake2b_keyed(g_pool, sizeof g_pool, g_pool, sizeof g_pool, msg, sizeof msg);
}
}  // namespace

void rng_stir(const void* data, size_t n) {
    crypto_blake2b_ctx ctx;
    crypto_blake2b_keyed_init(&ctx, sizeof g_pool, g_pool, sizeof g_pool);
    crypto_blake2b_update(&ctx, static_cast<const uint8_t*>(data), n);
    crypto_blake2b_final(&ctx, g_pool);
}

void rng_fill(uint8_t* out, size_t n) {
    uint8_t msg[64 + 8];
    uint8_t block[64];
    while (n > 0) {
        rng_hw_fill(msg, 64);
        g_counter++;
        memcpy(msg + 64, &g_counter, 8);
        crypto_blake2b_keyed(block, sizeof block, g_pool, sizeof g_pool, msg, sizeof msg);
        size_t take = n < sizeof block ? n : sizeof block;
        memcpy(out, block, take);
        out += take;
        n -= take;
    }
    ratchet();
    wipe(msg, sizeof msg);
    wipe(block, sizeof block);
}

uint32_t rng_uniform(uint32_t bound) {
    if (bound <= 1) return 0;
    // Rejection sampling: discard values in the biased tail.
    uint32_t limit = UINT32_MAX - (UINT32_MAX % bound);
    uint32_t v;
    do {
        rng_fill(reinterpret_cast<uint8_t*>(&v), sizeof v);
    } while (v >= limit);
    return v % bound;
}

}  // namespace cv

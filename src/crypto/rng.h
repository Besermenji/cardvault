// Cryptographically secure random numbers.
//
// Output = BLAKE2b(pool, hardware_random || counter). The pool is ratcheted
// after every call and can be stirred with extra entropy (e.g. key timings),
// so a weak hardware source alone does not make outputs predictable.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cv {

void rng_fill(uint8_t* out, size_t n);
void rng_stir(const void* data, size_t n);
// Unbiased random integer in [0, bound). bound must be > 0.
uint32_t rng_uniform(uint32_t bound);

// Platform hook: raw hardware randomness (rng_esp.cpp / rng_host.cpp).
void rng_hw_fill(uint8_t* out, size_t n);

}  // namespace cv

// Host (unit test) implementation of the hardware RNG hook.
#include <cstdio>
#include <cstdlib>

#include "crypto/rng.h"

namespace cv {

void rng_hw_fill(uint8_t* out, size_t n) {
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f || fread(out, 1, n, f) != n) abort();
    fclose(f);
}

}  // namespace cv

#include <bootloader_random.h>
#include <esp_system.h>

#include "crypto/rng.h"

namespace cv {

void rng_hw_fill(uint8_t* out, size_t n) {
    // Radios are never started, so enable the SAR ADC entropy source explicitly
    // to get true random numbers from the hardware RNG.
    bootloader_random_enable();
    esp_fill_random(out, n);
    bootloader_random_disable();
}

}  // namespace cv

/**
 * Hobbsless FFT Vibration Analysis
 *
 * Project Hobbsless
 * Description: SC-022
 * Author: Nick Garner
 * Date: Q4 2025
 *
 * (C) Skychair 2025
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef FILE_FFT_SEEN
#define FILE_FFT_SEEN

#include <stdint.h>
#include "esp_err.h"

#define FFT_SIZE        256     // Must be power of 2
#define SAMPLE_RATE_HZ  400     // LIS3DH ODR setting

typedef struct {
    float peak_freq_hz;         // Dominant frequency
    float peak_magnitude;       // Magnitude at peak frequency
    float total_energy;         // Total signal energy
} fft_result_t;

esp_err_t fft_init(void);
esp_err_t fft_collect_samples(void);
esp_err_t fft_compute(fft_result_t *result);

#endif /* !FILE_FFT_SEEN */

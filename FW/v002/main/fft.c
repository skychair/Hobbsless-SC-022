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

#include <string.h>
#include <math.h>
#include "fft.h"
#include "defs.h"
#include "lis3dh.h"

#include "esp_log.h"
#include "esp_dsp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

static char L_TAG[] = "FFT";

// Sample buffers
static float samples_x[FFT_SIZE];
static float samples_y[FFT_SIZE];
static float samples_z[FFT_SIZE];

// FFT working buffer (complex: real/imag interleaved)
static float fft_buf[FFT_SIZE * 2];

// Window coefficients
static float wind[FFT_SIZE];

esp_err_t fft_init(void) {
    esp_err_t ret;

    // Initialize FFT tables
    ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(L_TAG, "FFT init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Generate Hann window
    dsps_wind_hann_f32(wind, FFT_SIZE);

    ESP_LOGI(L_TAG, "Initialized (size=%d, rate=%dHz)", FFT_SIZE, SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t fft_collect_samples(void) {
    esp_err_t ret;
    int16_t x, y, z;

    // Set LIS3DH to 400Hz ODR for sampling
    // CTRL_REG1: 400Hz ODR, normal mode, all axes enabled
    ret = lis3dh_write_reg(LIS3DH_REG_CTRL_REG1, 0x77);
    if (ret != ESP_OK) {
        ESP_LOGE(L_TAG, "Failed to set ODR");
        return ret;
    }

    // Small delay for ODR change to take effect
    vTaskDelay(pdMS_TO_TICKS(10));

    // Collect samples at 400Hz (2500us interval)
    ESP_LOGI(L_TAG, "Collecting %d samples at %dHz...", FFT_SIZE, SAMPLE_RATE_HZ);

    int64_t sample_interval_us = 1000000 / SAMPLE_RATE_HZ;  // 2500us for 400Hz
    int64_t next_sample_time = esp_timer_get_time();

    for (int i = 0; i < FFT_SIZE; i++) {
        // Wait until it's time for the next sample
        while (esp_timer_get_time() < next_sample_time) {
            ets_delay_us(100);
        }
        next_sample_time += sample_interval_us;

        ret = lis3dh_read_accel(&x, &y, &z);
        if (ret != ESP_OK) {
            ESP_LOGE(L_TAG, "Read failed at sample %d", i);
            return ret;
        }

        // Convert to g and store (12-bit left-justified, 1mg/LSB at 12-bit)
        samples_x[i] = (x >> 4) / 1000.0f;
        samples_y[i] = (y >> 4) / 1000.0f;
        samples_z[i] = (z >> 4) / 1000.0f;
    }

    // Restore 10Hz ODR for low power
    lis3dh_write_reg(LIS3DH_REG_CTRL_REG1, 0x27);

    ESP_LOGI(L_TAG, "Sample collection complete");
    return ESP_OK;
}

static void compute_axis_fft(float *samples, float *peak_freq, float *peak_mag) {
    // Remove DC offset (mean) from samples
    float mean = 0;
    for (int i = 0; i < FFT_SIZE; i++) {
        mean += samples[i];
    }
    mean /= FFT_SIZE;

    // Copy samples to FFT buffer with DC removal and windowing
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_buf[i * 2] = (samples[i] - mean) * wind[i];  // Real (DC removed)
        fft_buf[i * 2 + 1] = 0;                           // Imaginary
    }

    // Compute FFT
    dsps_fft2r_fc32(fft_buf, FFT_SIZE);

    // Bit-reverse
    dsps_bit_rev_fc32(fft_buf, FFT_SIZE);

    // Find peak magnitude (skip DC at bin 0, only look at first half)
    float max_mag = 0;
    int max_bin = 1;

    for (int i = 1; i < FFT_SIZE / 2; i++) {
        float real = fft_buf[i * 2];
        float imag = fft_buf[i * 2 + 1];
        float mag = sqrtf(real * real + imag * imag);

        if (mag > max_mag) {
            max_mag = mag;
            max_bin = i;
        }
    }

    // Convert bin to frequency
    float freq_resolution = (float)SAMPLE_RATE_HZ / FFT_SIZE;
    *peak_freq = max_bin * freq_resolution;
    *peak_mag = max_mag / FFT_SIZE;  // Normalize
}

#define NOISE_THRESHOLD 0.005f  // Minimum magnitude to consider as real vibration

esp_err_t fft_compute(fft_result_t *result) {
    float peak_x, mag_x;
    float peak_y, mag_y;
    float peak_z, mag_z;

    // Compute FFT for each axis
    compute_axis_fft(samples_x, &peak_x, &mag_x);
    compute_axis_fft(samples_y, &peak_y, &mag_y);
    compute_axis_fft(samples_z, &peak_z, &mag_z);

    ESP_LOGI(L_TAG, "X: %.1fHz (mag=%.4f)", peak_x, mag_x);
    ESP_LOGI(L_TAG, "Y: %.1fHz (mag=%.4f)", peak_y, mag_y);
    ESP_LOGI(L_TAG, "Z: %.1fHz (mag=%.4f)", peak_z, mag_z);

    // Return the axis with highest magnitude as dominant vibration
    if (mag_x >= mag_y && mag_x >= mag_z) {
        result->peak_freq_hz = peak_x;
        result->peak_magnitude = mag_x;
    } else if (mag_y >= mag_x && mag_y >= mag_z) {
        result->peak_freq_hz = peak_y;
        result->peak_magnitude = mag_y;
    } else {
        result->peak_freq_hz = peak_z;
        result->peak_magnitude = mag_z;
    }

    // Calculate total energy across all axes
    result->total_energy = mag_x + mag_y + mag_z;

    // If below noise threshold, no significant vibration detected
    if (result->peak_magnitude < NOISE_THRESHOLD) {
        ESP_LOGI(L_TAG, "No significant vibration (below noise threshold)");
        result->peak_freq_hz = 0;
    } else {
        ESP_LOGI(L_TAG, "Dominant: %.1fHz, Energy: %.4f",
                 result->peak_freq_hz, result->total_energy);
    }

    return ESP_OK;
}

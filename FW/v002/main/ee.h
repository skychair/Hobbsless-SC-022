/**
 * Hobbsless EEPROM via NVS
 *
 * Project Hobbsless
 * Description: SC-022
 * Author: Nick Garner
 * Date: Q4 2025
 *
 * (C) Skychair 2025
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef FILE_HLEE_SEEN
#define FILE_HLEE_SEEN

#include <stdint.h>
#include "defs.h"

typedef struct {
    uint32_t start_time;         // epoch seconds
    uint32_t end_time;           // epoch seconds
    float    dominant_freq_hz;   // average dominant frequency across session
    float    peak_magnitude_g;   // peak acceleration magnitude in g
    float    avg_magnitude_g;    // average acceleration magnitude in g
    uint32_t sample_count;       // raw accelerometer samples (FFT calls * 256)
} engine_run_t;

void ee_init_if_needed();
void ee_init();
void ee_make_virgin();
uint8_t ee_open();
uint8_t ee_commit();
uint8_t ee_save_ssid_psk();
uint8_t ee_check_for_saved_wifi_creds();

uint16_t ee_get_pub_freq();
uint8_t ee_set_pub_freq(uint16_t new_freq);

uint16_t ee_get_ble_listen_duration();
uint8_t ee_set_ble_listen_duration(uint16_t new_ble_listen_duration);

uint8_t ee_get_ser_debug();
uint8_t ee_set_ser_debug(uint8_t enabled);

uint16_t ee_get_engine_mon_interval();
uint8_t ee_set_engine_mon_interval(uint16_t minutes);

uint16_t ee_get_engine_detect_samples();
uint8_t ee_set_engine_detect_samples(uint16_t samples);

uint8_t ee_get_post_flight_upload();
uint8_t ee_set_post_flight_upload(uint8_t enabled);

uint8_t ee_get_upload_queued();
uint8_t ee_set_upload_queued(uint8_t queued);

uint8_t ee_get_ota_queued();
uint8_t ee_set_ota_queued(uint8_t queued);

uint8_t ee_save_engine_run(uint32_t start_time, uint32_t end_time,
                            float dominant_freq_hz, float peak_magnitude_g,
                            float avg_magnitude_g, uint32_t sample_count);
uint8_t ee_get_engine_runs(engine_run_t *runs, uint8_t *count);
uint8_t ee_clear_engine_runs(void);


#endif /* !FILE_HLEE_SEEN */

/**
 * Hobbsless LED Control
 *
 * Project Hobbsless
 * Description: SC-022
 * Author: Nick Garner
 * Date: Q4 2025
 *
 * (C) Skychair 2025
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef FILE_LED_SEEN
#define FILE_LED_SEEN

void led_init();
void led_boot_sequence();
void led_ntp_synced();
void led_set(uint8_t led, uint8_t state);
void led_blink(uint8_t led, uint16_t ms);
void led_blink_yellow(uint16_t ms);
void led_morse_no_time();

#endif /* !FILE_LED_SEEN */

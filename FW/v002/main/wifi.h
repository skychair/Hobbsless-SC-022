/**
 * Hobbsless Wi-Fi
 *
 * Project Hobbsless
 * Description: SC-022
 * Author: Nick Garner
 * Date: Q4 2025
 *
 * (C) Skychair 2025
 * SPDX-License-Identifier: Apache-2.0
 */


uint8_t wifi_start(uint8_t *ssid, uint8_t *psk);
uint8_t wifi_stack_is_up(void);
uint8_t wifi_set_credentials(uint8_t *ssid, uint8_t *psk);
uint8_t wifi_connect(void);
uint8_t wifi_stop(void);
uint8_t wifi_shutdown(void);

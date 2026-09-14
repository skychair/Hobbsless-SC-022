/**
 * Hobbsless OTA Firmware Update
 *
 * Project Hobbsless
 * Description: SC-022
 * Author: Nick Garner
 * Date: Q3 2026
 *
 * (C) Skychair 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FILE_OTA_SEEN
#define FILE_OTA_SEEN

#include "esp_err.h"

/* Confirm the running image so the bootloader stops treating it as provisional.
 * Safe to call on every boot; does nothing unless a rollback is pending. */
void ota_mark_valid(void);

/* Ask the server for the preferred version and, if it differs from what is
 * running, download and install it. Reboots into the new image on success.
 *
 * Requires an active WiFi connection. Every failure path is non-fatal: the
 * running image is left untouched and the caller continues as normal. */
esp_err_t ota_check_and_update(void);

#endif /* !FILE_OTA_SEEN */

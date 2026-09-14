/**
 * Hobbsless HTTP Upload
 *
 * Project Hobbsless
 * Description: SC-022
 * Author: Nick Garner
 * Date: Q1 2026
 *
 * (C) Skychair 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FILE_HTTP_UPLOAD_SEEN
#define FILE_HTTP_UPLOAD_SEEN

#include "esp_err.h"
#include "ee.h"

typedef struct {
    int pub_freq_hours;   // -1 = no command received
} upload_response_t;

esp_err_t http_upload_runtime(const char *serial, const char *firmware_version,
                               engine_run_t *runs, uint8_t count,
                               upload_response_t *response_out);

#endif /* !FILE_HTTP_UPLOAD_SEEN */

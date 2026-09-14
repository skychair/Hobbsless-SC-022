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

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "defs.h"
#include "http_upload.h"

static char L_TAG[] = "HTTP";

/* Max JSON payload: 50 sessions * ~280 bytes each + envelope ~150 bytes */
#define JSON_BUF_SIZE  16384
#define RESP_BUF_SIZE  512

static void epoch_to_iso8601(uint32_t epoch, char *buf, size_t buf_len) {
    time_t t = (time_t)epoch;
    struct tm tm_info;
    gmtime_r(&t, &tm_info);
    strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}

typedef struct {
    char  buf[RESP_BUF_SIZE];
    int   len;
} resp_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    resp_ctx_t *ctx = (resp_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        int copy = evt->data_len;
        if (ctx->len + copy >= RESP_BUF_SIZE) {
            copy = RESP_BUF_SIZE - ctx->len - 1;
        }
        if (copy > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

esp_err_t http_upload_runtime(const char *serial, const char *firmware_version,
                               engine_run_t *runs, uint8_t count,
                               upload_response_t *response_out) {
    response_out->pub_freq_hours = -1;

    char *json = malloc(JSON_BUF_SIZE);
    if (!json) {
        ESP_LOGE(L_TAG, "Failed to allocate JSON buffer");
        return ESP_ERR_NO_MEM;
    }

    /* Build JSON payload */
    int pos = 0;
    pos += snprintf(json + pos, JSON_BUF_SIZE - pos,
                    "{\"uuid\":\"%s\",\"firmwareVersion\":\"%s\",\"hardwareVersion\":\"%s\",\"sessions\":[",
                    serial, firmware_version, HW_VERSION);

    for (int i = 0; i < count; i++) {
        char start_str[25], end_str[25];
        epoch_to_iso8601(runs[i].start_time, start_str, sizeof(start_str));
        epoch_to_iso8601(runs[i].end_time,   end_str,   sizeof(end_str));

        double duration_hours = (double)(runs[i].end_time - runs[i].start_time) / 3600.0;

        pos += snprintf(json + pos, JSON_BUF_SIZE - pos,
                        "%s{"
                        "\"startTime\":\"%s\","
                        "\"endTime\":\"%s\","
                        "\"duration\":%.4f,"
                        "\"vibrationProfile\":{"
                        "\"dominantFrequencyHz\":%.1f,"
                        "\"peakAmplitudeG\":%.3f,"
                        "\"avgAmplitudeG\":%.3f,"
                        "\"samples\":%lu"
                        "}"
                        "}",
                        (i > 0) ? "," : "",
                        start_str, end_str,
                        duration_hours,
                        (double)runs[i].dominant_freq_hz,
                        (double)runs[i].peak_magnitude_g,
                        (double)runs[i].avg_magnitude_g,
                        (unsigned long)runs[i].sample_count);

        if (pos >= JSON_BUF_SIZE - 1) {
            ESP_LOGE(L_TAG, "JSON buffer overflow at session %d", i);
            free(json);
            return ESP_ERR_NO_MEM;
        }
    }

    pos += snprintf(json + pos, JSON_BUF_SIZE - pos, "]}");
    ESP_LOGI(L_TAG, "POST %s%s (%d bytes, %d sessions)", SERVER_BASE_URL, SERVER_SYNC_PATH, pos, count);
    ESP_LOGI(L_TAG, "Payload: %s", json);

    /* Configure HTTP client */
    char url[128];
    snprintf(url, sizeof(url), "%s%s", SERVER_BASE_URL, SERVER_SYNC_PATH);

    resp_ctx_t resp_ctx = { .len = 0 };

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .timeout_ms        = 15000,
        .transport_type    = HTTP_TRANSPORT_UNKNOWN,  /* auto-detect from URL scheme */
        .crt_bundle_attach = esp_crt_bundle_attach,   /* used only for https:// URLs */
        .event_handler     = http_event_handler,
        .user_data         = &resp_ctx,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(L_TAG, "Failed to init HTTP client");
        free(json);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_post_field(client, json, pos);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(L_TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(json);
        return ESP_FAIL;
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(L_TAG, "HTTP status: %d", status);

    if (resp_ctx.len > 0) {
        ESP_LOGI(L_TAG, "Response: %s", resp_ctx.buf);
    }

    if (status != 200) {
        ESP_LOGE(L_TAG, "Server returned non-200 status: %d", status);
        esp_http_client_cleanup(client);
        free(json);
        return ESP_FAIL;
    }

    /* Scan response for pubFreq command */
    if (resp_ctx.len > 0) {
        char *pf = strstr(resp_ctx.buf, "\"pubFreq\"");
        if (pf) {
            pf = strchr(pf, ':');
            if (pf) {
                int val = atoi(pf + 1);
                if (val >= MIN_PUB_FREQ && val <= MAX_PUB_FREQ) {
                    response_out->pub_freq_hours = val;
                    ESP_LOGI(L_TAG, "Server command: pubFreq=%d", val);
                }
            }
        }
    }

    esp_http_client_cleanup(client);
    free(json);
    return ESP_OK;
}

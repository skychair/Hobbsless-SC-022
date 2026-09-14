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
 *
 * Flow: GET <SERVER_OTA_BASE_URL><SERVER_OTA_LATEST_PATH> returns the preferred
 * version identifier. If it differs from the version this build reports,
 * GET <SERVER_OTA_BASE_URL><SERVER_OTA_IMAGE_PATH><version><SERVER_OTA_IMAGE_SUFFIX>
 * streams the image into the inactive OTA slot and we reboot into it.
 * Those are defined in secretdefs.h, which is not in git.
 *
 * Nothing here is allowed to take the device down. Every failure - no server,
 * bad response, truncated download, bad image - logs and returns, leaving the
 * running firmware untouched.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "defs.h"
#include "led.h"
#include "util.h"
#include "ota.h"

static char L_TAG[] = "OTA";

#define OTA_RESP_BUF_SIZE 256

typedef struct {
    char buf[OTA_RESP_BUF_SIZE];
    int  len;          /* bytes kept in buf */
    int  total;        /* bytes actually received, may exceed buf */
    int  headers;      /* response headers seen */
    bool connected;    /* TCP+TLS established */
    bool got_headers;  /* server sent a complete header block */
} ota_resp_ctx_t;

/* Log a body safely: printable characters only, escaped, truncated. */
static void ota_log_body(const char *what, const char *buf, int len) {
    char out[OTA_RESP_BUF_SIZE * 2];
    int o = 0;
    for (int i = 0; i < len && o < (int)sizeof(out) - 5; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == '\n')      { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c == '\r') { out[o++] = '\\'; out[o++] = 'r'; }
        else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else if (c >= 0x20 && c < 0x7f) { out[o++] = (char)c; }
        else { o += snprintf(out + o, sizeof(out) - o, "\\x%02x", c); }
    }
    out[o] = '\0';
    ESP_LOGW(L_TAG, "  %s (%d bytes): \"%s\"", what, len, out);
}

static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt) {
    ota_resp_ctx_t *ctx = (ota_resp_ctx_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(L_TAG, "  http event: ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        if (ctx) ctx->connected = true;
        ESP_LOGI(L_TAG, "  connected to server (TLS up)");
        break;
    case HTTP_EVENT_HEADERS_SENT:
        ESP_LOGI(L_TAG, "  request sent, waiting for response");
        break;
    case HTTP_EVENT_ON_HEADER:
        if (ctx) ctx->headers++;
        ESP_LOGI(L_TAG, "  < %s: %s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        if (ctx) {
            ctx->got_headers = true;
            ctx->total += evt->data_len;
            int copy = evt->data_len;
            if (ctx->len + copy >= OTA_RESP_BUF_SIZE) {
                copy = OTA_RESP_BUF_SIZE - ctx->len - 1;
            }
            if (copy > 0) {
                memcpy(ctx->buf + ctx->len, evt->data, copy);
                ctx->len += copy;
                ctx->buf[ctx->len] = '\0';
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(L_TAG, "  response complete");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(L_TAG, "  disconnected");
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* Pull the version identifier out of the /latest response.
 * Accepts a bare string ("1.1.4"), a quoted one, or {"version":"1.1.4"} -
 * the sibling sync endpoint returns JSON, so both shapes are plausible. */
static uint8_t ota_extract_version(const char *body, char *out, size_t out_len) {
    const char *start = body;
    const char *end;

    const char *key = strstr(body, "\"version\"");
    if (key) {
        key = strchr(key + 9, ':');
        if (!key) return NO;
        start = strchr(key, '"');
        if (!start) return NO;
        start++;
        end = strchr(start, '"');
        if (!end) return NO;
    } else {
        while (*start && isspace((unsigned char)*start)) start++;
        if (*start == '"') {
            start++;
            end = strchr(start, '"');
            if (!end) return NO;
        } else {
            end = start;
            while (*end && !isspace((unsigned char)*end)) end++;
        }
    }

    size_t len = (size_t)(end - start);
    if (len == 0 || len >= out_len) {
        ESP_LOGE(L_TAG, "version string length out of range: %d", (int)len);
        return NO;
    }

    /* Only characters that are safe to paste into a URL path segment. */
    for (size_t i = 0; i < len; i++) {
        char c = start[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != '-') {
            ESP_LOGE(L_TAG, "illegal character in version string: 0x%02x", (unsigned char)c);
            return NO;
        }
    }

    memcpy(out, start, len);
    out[len] = '\0';
    return YES;
}

/* GET /firmware/sc-022/latest. Returns ESP_OK and fills version[] on success. */
static esp_err_t ota_fetch_latest_version(char *version, size_t version_len) {
    char url[128];
    snprintf(url, sizeof(url), "%s%s", SERVER_OTA_BASE_URL, SERVER_OTA_LATEST_PATH);

    ota_resp_ctx_t resp = { .len = 0 };
    resp.buf[0] = '\0';

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .timeout_ms        = OTA_LATEST_TIMEOUT_MS,
        .event_handler     = ota_http_event_handler,
        .user_data         = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(L_TAG, "failed to init HTTP client for %s", url);
        return ESP_FAIL;
    }

    ESP_LOGI(L_TAG, "version check: GET %s (timeout %dms)", url, OTA_LATEST_TIMEOUT_MS);

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int elapsed_ms = (int)((esp_timer_get_time() - t0) / 1000);

    int status = esp_http_client_get_status_code(client);
    int content_len = (int)esp_http_client_get_content_length(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(L_TAG, "version check failed after %dms: %s", elapsed_ms, esp_err_to_name(err));
        ESP_LOGE(L_TAG, "  url=%s", url);
        ESP_LOGE(L_TAG, "  tls_connected=%s request_sent=%s headers_received=%d body_bytes=%d",
                 resp.connected ? "yes" : "no",
                 resp.connected ? "yes" : "no",
                 resp.headers, resp.total);
        if (resp.len > 0) {
            ota_log_body("partial body", resp.buf, resp.len);
        }
        if (err == ESP_ERR_HTTP_EAGAIN) {
            ESP_LOGE(L_TAG, "  EAGAIN = the timeout elapsed with no response.");
            if (resp.connected && resp.headers == 0) {
                ESP_LOGE(L_TAG, "  TLS came up but the server sent no headers - the route is");
                ESP_LOGE(L_TAG, "  accepting the connection and then hanging. Check that the");
                ESP_LOGE(L_TAG, "  handler actually returns/flushes a response, and that it is");
                ESP_LOGE(L_TAG, "  not waiting on something slower than %dms.", OTA_LATEST_TIMEOUT_MS);
            } else if (resp.headers > 0) {
                ESP_LOGE(L_TAG, "  Headers arrived but the body did not. Check Content-Length");
                ESP_LOGE(L_TAG, "  matches the bytes sent, and that the response is not chunked");
                ESP_LOGE(L_TAG, "  and left open.");
            }
        }
        return ESP_FAIL;
    }

    ESP_LOGI(L_TAG, "version check: HTTP %d in %dms, content_length=%d, body_bytes=%d",
             status, elapsed_ms, content_len, resp.total);

    if (status != 200) {
        ESP_LOGE(L_TAG, "version check returned status %d for %s", status, url);
        if (resp.len > 0) {
            ota_log_body("error body", resp.buf, resp.len);
        }
        return ESP_FAIL;
    }
    if (resp.len == 0) {
        ESP_LOGE(L_TAG, "version check returned HTTP 200 with an empty body (%s)", url);
        ESP_LOGE(L_TAG, "  expected a version string such as 2.0.3, \"2.0.3\", or {\"version\":\"2.0.3\"}");
        return ESP_FAIL;
    }

    ota_log_body("body", resp.buf, resp.len);

    if (resp.total > resp.len) {
        ESP_LOGW(L_TAG, "  body was truncated at %d of %d bytes - a version response",
                 resp.len, resp.total);
        ESP_LOGW(L_TAG, "  should be far smaller than %d bytes", OTA_RESP_BUF_SIZE);
    }

    if (ota_extract_version(resp.buf, version, version_len) != YES) {
        ESP_LOGE(L_TAG, "could not parse a version from the body above");
        ESP_LOGE(L_TAG, "  accepted: 2.0.3 | \"2.0.3\" | {\"version\":\"2.0.3\"}");
        ESP_LOGE(L_TAG, "  allowed characters: A-Z a-z 0-9 . _ -   max length %d", OTA_VERSION_MAX_LEN - 1);
        return ESP_FAIL;
    }

    ESP_LOGI(L_TAG, "server prefers version '%s', running '%s'", version, fw_version());
    return ESP_OK;
}

void ota_mark_valid(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return;
    }

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(L_TAG, "new image on '%s' confirmed, rollback cancelled", running->label);
        } else {
            ESP_LOGE(L_TAG, "failed to confirm image: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t ota_check_and_update(void) {
    char version[OTA_VERSION_MAX_LEN];

    esp_err_t err = ota_fetch_latest_version(version, sizeof(version));
    if (err != ESP_OK) {
        ESP_LOGW(L_TAG, "skipping update, version check did not succeed");
        return err;
    }

    if (strcmp(version, fw_version()) == 0) {
        ESP_LOGI(L_TAG, "firmware is up to date");
        return ESP_OK;
    }

    char url[160];
    snprintf(url, sizeof(url), "%s%s%s%s", SERVER_OTA_BASE_URL, SERVER_OTA_IMAGE_PATH,
             version, SERVER_OTA_IMAGE_SUFFIX);
    ESP_LOGW(L_TAG, "updating %s -> %s from %s", fw_version(), version, url);

    esp_http_client_config_t http_config = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .timeout_ms        = OTA_DOWNLOAD_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    /* White while downloading (STATE_OTA) - LEDs are active low */
    led_set(RED, OFF);
    led_set(GRN, OFF);
    led_set(BLU, OFF);

    err = esp_https_ota(&ota_config);

    led_set(RED, ON);
    led_set(GRN, ON);
    led_set(BLU, ON);

    if (err != ESP_OK) {
        /* Image was not written, or was written but failed validation. Either
         * way the boot partition is unchanged and we keep running as we were. */
        ESP_LOGE(L_TAG, "update failed: %s - staying on %s", esp_err_to_name(err), fw_version());
        return err;
    }

    ESP_LOGW(L_TAG, "update to %s installed, rebooting", version);
    uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);
    esp_restart();

    return ESP_OK;  /* not reached */
}

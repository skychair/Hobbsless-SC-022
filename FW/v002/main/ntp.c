/**
 * Hobbsless NTP
 *
 * Project Hobbsless
 * Description: SC-022
 * Author: Nick Garner
 * Date: Q4 2025
 *
 * (C) Skychair 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "defs.h"
#include "ntp.h"
#include "led.h"

#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"

static char L_TAG[] = "NTP";


uint8_t is_time_valid() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2024 - 1900)) {
        ESP_LOGI(L_TAG, "is_time_valid():: Time is not set yet.");
        return NO;
    }
    log_current_time();
    ESP_LOGI(L_TAG, "is_time_valid():: Time is valid.");
    return YES;
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(L_TAG, "Notification of a time synchronization event");
}


uint8_t sync_sntp_time() {
    ESP_LOGI(L_TAG, "sync_sntp_time:: start");

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
    config.sync_cb = time_sync_notification_cb;
    esp_netif_sntp_init(&config);

    //print_servers();

    // wait for time to be set
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(L_TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }

    esp_netif_sntp_deinit();

    /* The wait loop above ends either on a sync or on running out of retries,
     * so ask the clock itself whether it actually got set. */
    if (is_time_valid() == NO) {
        ESP_LOGE(L_TAG, "sync_sntp_time:: failed to get time after %d retries", retry_count);
        return NO;
    }

    ESP_LOGI(L_TAG, "sync_sntp_time:: time synced");
    led_ntp_synced();
    return YES;
}

void log_current_time() {
    char strftime_buf[64];
    time_t now = 0;
    struct tm timeinfo = { 0 };
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGW(L_TAG, "The current UTC date/time is: %s", strftime_buf);
}


uint32_t get_epoch_time() {
    time_t now;
    time(&now);
    return (uint32_t)now;
}

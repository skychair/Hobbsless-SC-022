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


#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ee.h"
#include "defs.h"

#include "lwip/err.h"
#include "lwip/sys.h"


static char L_TAG[] = "WiFi";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define H2E_IDENTIFIER ""
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK


/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static bool s_intentional_disconnect = false;

extern int8_t wifi_is_connected;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Don't auto-connect here - let wifi_connect() handle it explicitly
        ESP_LOGI(L_TAG, "WiFi STA started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Don't retry if this was an intentional disconnect
        if (s_intentional_disconnect) {
            ESP_LOGI(L_TAG, "Intentional disconnect - not retrying");
            s_intentional_disconnect = false;
            return;
        }

        if (s_retry_num < ESP_WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(L_TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(L_TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(L_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * Initialize WiFi stack and configure credentials (call once at startup)
 * Does not connect - call wifi_connect() after this
 */
/**
 * Has the WiFi stack been initialized this boot?
 * wifi_start() creates the event group, wifi_shutdown() destroys it.
 */
uint8_t wifi_stack_is_up(void) {
    return (s_wifi_event_group != NULL) ? YES : NO;
}


/**
 * Apply SSID/PSK to the (already initialized) WiFi driver.
 * Only valid after esp_wifi_init() - i.e. after wifi_start().
 */
uint8_t wifi_set_credentials(uint8_t *ssid, uint8_t *psk) {
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = H2E_IDENTIFIER,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, (char *)ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, (char *)psk, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    ESP_LOGI(L_TAG, "Configuring WiFi for SSID: %s", ssid);

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(L_TAG, "Failed to set WiFi config: %d", ret);
        return NO;
    }
    return YES;
}


uint8_t wifi_start(uint8_t *ssid, uint8_t *psk) {
    ESP_LOGI(L_TAG, "Initializing WiFi stack...");

    wifi_is_connected = NO;

    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(L_TAG, "Failed to create event group");
        return NO;
    }

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // Set WiFi mode to station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Configure WiFi with credentials
    if (wifi_set_credentials(ssid, psk) != YES) {
        return NO;
    }

    // Start WiFi (but don't connect yet)
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(L_TAG, "WiFi stack initialized and configured successfully.");
    return YES;
}


/**
 * Connect to WiFi using already-configured credentials
 * Call after wifi_start() has been called
 */
uint8_t wifi_connect(void) {
    ESP_LOGI(L_TAG, "Connecting to WiFi...");

    wifi_is_connected = NO;

    // Check if event group exists (WiFi should be initialized)
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(L_TAG, "WiFi not initialized! Call wifi_start() first.");
        return NO;
    }

    // Reset retry counter
    s_retry_num = 0;

    // Clear previous event bits
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // Trigger connection attempt
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(L_TAG, "Failed to start WiFi connection: %d", ret);
        return NO;
    }

    // Wait for connection result
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    // Check result
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(L_TAG, "Connected successfully");
        wifi_is_connected = YES;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(L_TAG, "Failed to connect");
        wifi_is_connected = NO;
    } else {
        ESP_LOGE(L_TAG, "Unexpected event during WiFi connection");
        wifi_is_connected = NO;
    }

    ESP_LOGI(L_TAG, "Connection attempt finished: %d", wifi_is_connected);
    return wifi_is_connected;
}


uint8_t wifi_stop(void) {
    ESP_LOGI(L_TAG, "Disconnecting WiFi...");

    // Set flag to prevent auto-reconnect on disconnect event
    s_intentional_disconnect = true;

    // Disconnect from AP (but keep WiFi initialized for later reconnection)
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(L_TAG, "Failed to disconnect WiFi: %d", ret);
        s_intentional_disconnect = false;
    }

    wifi_is_connected = NO;

    ESP_LOGI(L_TAG, "WiFi disconnected (still initialized for reconnection)");
    return wifi_is_connected;
}


uint8_t wifi_shutdown(void) {
    ESP_LOGI(L_TAG, "Shutting down WiFi stack...");

    wifi_is_connected = NO;

    // Set flag to prevent auto-reconnect on disconnect event
    s_intentional_disconnect = true;

    // Stop WiFi
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(L_TAG, "Failed to stop WiFi: %d", ret);
    }

    // Deinitialize WiFi
    ret = esp_wifi_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(L_TAG, "Failed to deinit WiFi: %d", ret);
        s_intentional_disconnect = false;
        return NO;
    }

    // Clean up event group
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    s_intentional_disconnect = false;

    ESP_LOGI(L_TAG, "WiFi stack shut down successfully");
    return YES;
}



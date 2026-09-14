/**
 * Hobbsless BLE Command and Control
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
#include <stdlib.h>
#include "ble_cc.h"
#include "defs.h"

#include "esp_log.h"
#include "ee.h"
#include "util.h"
#include "wifi.h"

/* BLE for notifications */
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

static char L_TAG[] = "BLE_CC";

/* External variables from main */
extern uint8_t ssid[33];
extern uint8_t psk[65];
extern uint16_t pub_freq;
extern uint16_t ble_listen_duration;
extern uint16_t engine_mon_interval;
extern uint16_t engine_detect_samples;
extern int8_t wifi_is_connected;

/**
 * Helper function to send BLE notification with string data
 */
static int send_ble_response(uint16_t conn_handle, uint16_t attr_handle, const char *response) {
  if (response == NULL) {
    return -1;
  }

  size_t len = strlen(response);
  struct os_mbuf *txom = ble_hs_mbuf_from_flat(response, len);
  if (txom) {
    int rc = ble_gatts_notify_custom(conn_handle, attr_handle, txom);
    if (rc != 0) {
      ESP_LOGE(L_TAG, "Failed to send response; rc=%d", rc);
      return -1;
    }
    ESP_LOGI(L_TAG, "Sent response: %s", response);
    return 0;
  }
  return -1;
}

/**
 * Parse and execute BLE command
 * Command format: C|arg
 * where C is a single character command and arg is the argument
 */
uint8_t parse_from_ble(const char *cmd_str, uint16_t conn_handle, uint16_t attr_handle) {
  if (cmd_str == NULL || strlen(cmd_str) < 3) {
    ESP_LOGE(L_TAG, "Invalid command string");
    return 0;
  }

  // Parse command: format is "C|arg"
  char cmd = cmd_str[0];

  // Check for delimiter
  if (cmd_str[1] != '|') {
    ESP_LOGE(L_TAG, "Invalid command format - missing delimiter");
    return 0;
  }

  // Get argument (everything after the |)
  const char *arg = &cmd_str[2];

  ESP_LOGI(L_TAG, "Received command: '%c' with arg: '%s'", cmd, arg);

  // Process commands
  switch (cmd) {
    case 'v':
      // Make virgin command
      if (strcmp(arg, "1") == 0) {
        ESP_LOGI(L_TAG, "Executing make_virgin command");
        ee_make_virgin();
        return 1;
      } else {
        ESP_LOGE(L_TAG, "Invalid argument for 'v' command: %s", arg);
        return 0;
      }
      break;

    case 'r':
      // Reboot command
      if (strcmp(arg, "1") == 0) {
        ESP_LOGI(L_TAG, "Executing reboot command");
        reboot(1);
        return 1;
      } else {
        ESP_LOGE(L_TAG, "Invalid argument for 'r' command: %s", arg);
        return 0;
      }
      break;

    case 's':
      // Save SSID command
      if (strlen(arg) > 0 && strlen(arg) < sizeof(ssid)) {
        ESP_LOGI(L_TAG, "Saving SSID: %s", arg);
        memset(ssid, 0, sizeof(ssid));
        strncpy((char*)ssid, arg, sizeof(ssid) - 1);
        ee_save_ssid_psk();
        return 1;
      } else {
        ESP_LOGE(L_TAG, "Invalid SSID length: %d (max %d)", strlen(arg), sizeof(ssid) - 1);
        return 0;
      }
      break;

    case 'c':
      // Connect Wifi
      if (strcmp(arg, "1") == 0) {
        ESP_LOGI(L_TAG, "Connect Wi-Fi");
        if (strlen((char*)ssid) == 0) {
          ESP_LOGE(L_TAG, "No SSID set - send 's|<ssid>' and 'p|<psk>' first");
          return 0;
        }
        /* The stack is only brought up at boot when creds already existed, so on
         * a first-time config session it is not running yet. Start it here, or
         * re-apply the creds the user just sent if it is already up. */
        if (wifi_stack_is_up() == YES) {
          wifi_stop();  // drop any existing association before reconfiguring
          if (wifi_set_credentials(ssid, psk) != YES) {
            return 0;
          }
        } else if (wifi_start(ssid, psk) != YES) {
          ESP_LOGE(L_TAG, "Failed to start WiFi stack");
          return 0;
        }
        wifi_connect();
        return wifi_is_connected;
      } else {
        ESP_LOGE(L_TAG, "Invalid argument for 'c' command: %s", arg);
        return 0;
      }
      break;

    case 'p':
      // Save passphrase command
      if (strlen(arg) > 0 && strlen(arg) < sizeof(psk)) {
        ESP_LOGI(L_TAG, "Saving passphrase");
        memset(psk, 0, sizeof(psk));
        strncpy((char*)psk, arg, sizeof(psk) - 1);
        ee_save_ssid_psk();
        return 1;
      } else {
        ESP_LOGE(L_TAG, "Invalid passphrase length: %d (max %d)", strlen(arg), sizeof(psk) - 1);
        return 0;
      }
      break;

    case 'f':
      // Set pub_freq command
      {
        uint16_t new_freq = (uint16_t)atoi(arg);
        if (new_freq >= MIN_PUB_FREQ && new_freq <= MAX_PUB_FREQ) {
          ESP_LOGI(L_TAG, "Setting pub_freq to: %d", new_freq);
          if (ee_set_pub_freq(new_freq)) {
            pub_freq = new_freq;
            return 1;
          } else {
            ESP_LOGE(L_TAG, "Failed to set pub_freq");
            return 0;
          }
        } else {
          ESP_LOGE(L_TAG, "pub_freq out of range: %d (valid: %d-%d)", new_freq, MIN_PUB_FREQ, MAX_PUB_FREQ);
          return 0;
        }
      }
      break;

    case 'b':
      // Set ble_listen_duration command
      {
        uint16_t new_duration = (uint16_t)atoi(arg);
        if (new_duration >= MIN_BLE_LISTEN_DURATION && new_duration <= MAX_BLE_LISTEN_DURATION) {
          ESP_LOGI(L_TAG, "Setting ble_listen_duration to: %d", new_duration);
          if (ee_set_ble_listen_duration(new_duration)) {
            ble_listen_duration = new_duration;
            return 1;
          } else {
            ESP_LOGE(L_TAG, "Failed to set ble_listen_duration");
            return 0;
          }
        } else {
          ESP_LOGE(L_TAG, "ble_listen_duration out of range: %d (valid: %d-%d)", new_duration, MIN_BLE_LISTEN_DURATION, MAX_BLE_LISTEN_DURATION);
          return 0;
        }
      }
      break;

    case 'x':
      // Force upload now - sets queued flag and reboots
      if (strcmp(arg, "1") == 0) {
        ESP_LOGI(L_TAG, "Force upload queued, rebooting");
        ee_set_upload_queued(ON);
        reboot(1);
        return 1;
      } else {
        ESP_LOGE(L_TAG, "Invalid argument for 'x' command: %s", arg);
        return 0;
      }
      break;

    case 'm':
      // Set engine monitor interval (minutes)
      {
        uint16_t new_interval = (uint16_t)atoi(arg);
        if (new_interval >= MIN_ENGINE_MON_INTERVAL && new_interval <= MAX_ENGINE_MON_INTERVAL) {
          ESP_LOGI(L_TAG, "Setting engine_mon_interval to: %d min", new_interval);
          if (ee_set_engine_mon_interval(new_interval)) {
            engine_mon_interval = new_interval;
            return 1;
          } else {
            ESP_LOGE(L_TAG, "Failed to set engine_mon_interval");
            return 0;
          }
        } else {
          ESP_LOGE(L_TAG, "engine_mon_interval out of range: %d (valid: %d-%d)",
                   new_interval, MIN_ENGINE_MON_INTERVAL, MAX_ENGINE_MON_INTERVAL);
          return 0;
        }
      }
      break;

    case 'o':
      // Queue an OTA check - runs on next boot, once WiFi is up.
      // Deferred rather than run inline: BLE, WiFi and TLS together is a lot
      // of heap, and the download reboots the device anyway.
      if (strcmp(arg, "1") == 0) {
        ESP_LOGI(L_TAG, "OTA check queued, rebooting");
        ee_set_ota_queued(ON);
        reboot(1);
        return 1;
      } else {
        ESP_LOGE(L_TAG, "Invalid argument for 'o' command: %s", arg);
        return 0;
      }
      break;

    case 'd':
      // Set engine detection FFT sample count
      {
        uint16_t new_samples = (uint16_t)atoi(arg);
        if (new_samples >= MIN_ENGINE_DETECT_SAMPLES && new_samples <= MAX_ENGINE_DETECT_SAMPLES) {
          ESP_LOGI(L_TAG, "Setting engine_detect_samples to: %d", new_samples);
          if (ee_set_engine_detect_samples(new_samples)) {
            engine_detect_samples = new_samples;
            return 1;
          } else {
            ESP_LOGE(L_TAG, "Failed to set engine_detect_samples");
            return 0;
          }
        } else {
          ESP_LOGE(L_TAG, "engine_detect_samples out of range: %d (valid: %d-%d)",
                   new_samples, MIN_ENGINE_DETECT_SAMPLES, MAX_ENGINE_DETECT_SAMPLES);
          return 0;
        }
      }
      break;

    case 'u':
      // Set post-flight upload flag
      if (strcmp(arg, "1") == 0) {
        ee_set_post_flight_upload(ON);
        return 1;
      } else if (strcmp(arg, "0") == 0) {
        ee_set_post_flight_upload(OFF);
        return 1;
      } else {
        ESP_LOGE(L_TAG, "Invalid argument for 'u' command: %s", arg);
        return 0;
      }
      break;

    case 'g':
      // Get value commands
      {
        char response[128];

        if (strcmp(arg, "pf") == 0) {
          // Get pub_freq
          snprintf(response, sizeof(response), "%u", pub_freq);
          ESP_LOGI(L_TAG, "Getting pub_freq: %s", response);
          return (send_ble_response(conn_handle, attr_handle, response) == 0) ? 1 : 0;

        } else if (strcmp(arg, "bld") == 0) {
          // Get ble_listen_duration
          snprintf(response, sizeof(response), "%u", ble_listen_duration);
          ESP_LOGI(L_TAG, "Getting ble_listen_duration: %s", response);
          return (send_ble_response(conn_handle, attr_handle, response) == 0) ? 1 : 0;

        } else if (strcmp(arg, "eds") == 0) {
          // Get engine_detect_samples
          snprintf(response, sizeof(response), "%u", engine_detect_samples);
          ESP_LOGI(L_TAG, "Getting engine_detect_samples: %s", response);
          return (send_ble_response(conn_handle, attr_handle, response) == 0) ? 1 : 0;

        } else if (strcmp(arg, "s") == 0) {
          // Get SSID
          ESP_LOGI(L_TAG, "Getting SSID: %s", (char*)ssid);
          return (send_ble_response(conn_handle, attr_handle, (char*)ssid) == 0) ? 1 : 0;

        } else if (strcmp(arg, "p") == 0) {
          // Get passphrase
          ESP_LOGI(L_TAG, "Getting passphrase");
          return (send_ble_response(conn_handle, attr_handle, (char*)psk) == 0) ? 1 : 0;

        } else if (strcmp(arg, "pfu") == 0) {
          // Get post-flight upload setting
          snprintf(response, sizeof(response), "%u", ee_get_post_flight_upload());
          return (send_ble_response(conn_handle, attr_handle, response) == 0) ? 1 : 0;

        } else if (strcmp(arg, "v") == 0) {
          // Get firmware version
          return (send_ble_response(conn_handle, attr_handle, fw_version()) == 0) ? 1 : 0;

        } else if (strcmp(arg, "emi") == 0) {
          // Get engine monitor interval
          snprintf(response, sizeof(response), "%u", engine_mon_interval);
          return (send_ble_response(conn_handle, attr_handle, response) == 0) ? 1 : 0;

        } else {
          ESP_LOGE(L_TAG, "Unknown get sub-command: %s", arg);
          send_ble_response(conn_handle, attr_handle, "error");
          return 0;
        }
      }
      break;

    default:
      ESP_LOGE(L_TAG, "Unknown command: '%c'", cmd);
      return 0;
  }

  return 0;
}

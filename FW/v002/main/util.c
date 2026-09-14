/**
 * Hobbsless Utils
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
#include "util.h"
#include "defs.h"
#include "led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "time.h"


static char L_TAG[] = "UTIL";

/* Reboot in X seconds */
void reboot(uint8_t secs) {
  // Restart module
  for (int i = secs; i >= 0; i--) {
    led_blink_yellow(80);   /* one yellow blink per second of countdown */
    ESP_LOGW(L_TAG, "Restarting in %d seconds...", i);
    vTaskDelay(pdMS_TO_TICKS(1000 - 80));
  }
  ESP_LOGE(L_TAG, "Restarting now."); 
  fflush(stdout);
  esp_restart();
}

const char *fw_version(void) {
  return esp_app_get_description()->version;
}

void get_mac_uuid(uint8_t *mac_uuid, char *mac_uuid_str) {
  esp_err_t err = esp_read_mac(mac_uuid, ESP_MAC_WIFI_STA);

  switch (err) {
    case ESP_OK:
      sprintf(mac_uuid_str, "%02x%02x%02x%02x%02x%02x", 
        mac_uuid[0],
        mac_uuid[1],
        mac_uuid[2],
        mac_uuid[3],
        mac_uuid[4],
        mac_uuid[5]);
      ESP_LOGI(L_TAG, "get_mac_uuid():: str: %s", mac_uuid_str); 
      break;
    default :
      printf("Error something: %s\n", esp_err_to_name(err));
  }

}


int get_epoch() {
  //time_t now;
  //time(&now);
  return (int)time(NULL);
}

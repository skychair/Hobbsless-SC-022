/**
 * Hobbsless EEPROM via NVS
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
#include "ee.h"
#include "defs.h"

#include "esp_log.h"
#include "util.h"

/* NVS */
#include "nvs.h"
#include "nvs_flash.h"


extern uint8_t ssid[33];
extern uint8_t psk[65];
extern uint16_t pub_freq;
extern uint16_t ble_listen_duration;

static char L_TAG[] = "EE";

nvs_handle_t ee_handle;
esp_err_t err;
bool ee_is_open = false;

uint8_t ee_open() {
  ESP_LOGI(L_TAG, "Opening Non-Volatile Storage (NVS) handle... ");

  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK( err );

  err = nvs_open("storage", NVS_READWRITE, &ee_handle);
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    return false;
  } else {
    ee_is_open = true;
    return true;
  }
  return false;
}

uint8_t ee_commit() {
    // Commit
    ESP_LOGI(L_TAG, "Committing updates in NVS ... ");

    err = nvs_commit(ee_handle);

    if (err == ESP_OK) {
      ESP_LOGI(L_TAG, "Done.");
    } else {
      ESP_LOGE(L_TAG, "Failed!");
    }
    return true;
}


/* check for magic value and set defaults at boot if not exist */
void ee_init_if_needed() {
  //check the value of cherry, if == 0x73, it's been init'd
  // if != 0x73, init it.
  ESP_LOGI(L_TAG, "ee_init_if_needed()::");
  uint8_t cherry = 0x00;
  esp_err_t err = nvs_get_u8(ee_handle, EE_VIRGIN_KEY, &cherry);
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error getting from nvs, probably not exist."); }

  ESP_LOGI(L_TAG, "Check is 0x%02x", cherry);
  if (cherry != EE_INITD_VALUE) {
    ESP_LOGI(L_TAG, "EE is virgin, calling ee_init()");
    ee_init();
  } else {
    ESP_LOGI(L_TAG, "Not init'ing EE");
  }
}

void ee_init() {
  esp_err_t err;

  bzero(&ssid, sizeof(ssid));
  bzero(&psk, sizeof(psk));

  err = nvs_set_u8(ee_handle, EE_SER_DEBUG_KEY, EE_DEFAULT_SER_DEBUG);
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); } else { ESP_LOGI(L_TAG, "saved ser_debug default value"); }
  err = nvs_set_blob(ee_handle, EE_SAVED_SSID_KEY, ssid, sizeof(ssid));
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); } else { ESP_LOGI(L_TAG, "saved ssid default value"); }
  err = nvs_set_blob(ee_handle, EE_SAVED_PSK_KEY, psk, sizeof(psk));
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); } else { ESP_LOGI(L_TAG, "saved psd default value"); }
  err = nvs_set_u8(ee_handle, EE_VIRGIN_KEY, EE_INITD_VALUE);
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); } else { ESP_LOGI(L_TAG, "saved init'd value"); }

  err = nvs_set_u16(ee_handle, EE_PUB_FREQ_KEY, EE_DEFAULT_PUB_FREQ);
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); } else { ESP_LOGI(L_TAG, "saved pub_freq default value"); }

  err = nvs_set_u16(ee_handle, EE_BLE_LISTEN_DURATION_KEY, EE_DEFAULT_BLE_LISTEN_DURATION);
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); } else { ESP_LOGI(L_TAG, "saved ble_listen_duration default value"); }

  err = nvs_set_u8(ee_handle, EE_POST_FLIGHT_UPLOAD_KEY, EE_DEFAULT_POST_FLIGHT_UPLOAD);
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); } else { ESP_LOGI(L_TAG, "saved auto_upload default value"); }

  err = nvs_set_u16(ee_handle, EE_ENGINE_MON_INTERVAL_KEY, EE_DEFAULT_ENGINE_MON_INTERVAL);
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); } else { ESP_LOGI(L_TAG, "saved engine_mon_interval default value"); }

  err = nvs_set_u16(ee_handle, EE_ENGINE_DETECT_SAMPLES_KEY, EE_DEFAULT_ENGINE_DETECT_SAMPLES);
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); } else { ESP_LOGI(L_TAG, "saved engine_detect_samples default value"); }

  ee_commit();
}

/* dirty the magic byte and reboot */
void ee_make_virgin() {
  ESP_LOGI(L_TAG, "ee_make_virgin()::");
  esp_err_t err = nvs_set_u8(ee_handle, EE_VIRGIN_KEY, 0x00);
  if (err != ESP_OK) { ESP_LOGE(L_TAG, "Error saving to nvs."); }
  ee_commit();
  reboot(5);
}

/* get value of debug, true/false */
uint8_t ee_get_ser_debug() {
  uint8_t ser_debug;
  esp_err_t err = nvs_get_u8(ee_handle, EE_SER_DEBUG_KEY, &ser_debug);
  if (err != ESP_OK) {
    //ESP_LOGE(L_TAG, "Error getting ser_debug value from NVS");
    return false;
  } else {
    //ESP_LOGI(L_TAG, "returning ser_debug value of: %d", ser_debug);
    return ser_debug;
  }
}

/* set the value of debug, true/false */
uint8_t ee_set_ser_debug(uint8_t ser_debug) {
  ESP_LOGI(L_TAG, "ee_set_ser_debug():: %d", ser_debug);

  //set it now
  if (ser_debug == ON) {
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(L_TAG, "ser_debug enabled");
  } else {
    esp_log_level_set("*", ESP_LOG_WARN);
    ESP_LOGI(L_TAG, "ser_debug disabled");
  }

  //save it for later
  esp_err_t err = nvs_set_u8(ee_handle, EE_SER_DEBUG_KEY, ser_debug);
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "Error setting ser_debug value to NVS");
    return false;
  } else {
    ESP_LOGI(L_TAG, "set ser_debug to value: %d in NVS", ser_debug);
    ee_commit();
    return true;
  }


}


/* check for saved wifi credentials */
uint8_t ee_check_for_saved_wifi_creds() {
  // Initialize NVS
  /*
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK( err );
  */

  // Open

  if (ee_is_open != true) {
      ESP_LOGE(L_TAG, "EE not open (%s)!", esp_err_to_name(err));
  } else {
    // Read
    ESP_LOGI(L_TAG, "Reading saved SSID and PSK");
    //memset(&ssid[0], 0, sizeof(ssid));
    //memset(&psk[0], 0, sizeof(psk));
    bzero(&ssid, sizeof(ssid));
    bzero(&psk, sizeof(psk));

    size_t required_size = sizeof(ssid);
    err = nvs_get_blob(ee_handle, EE_SAVED_SSID_KEY, ssid, &required_size);

    switch (err) {
      case ESP_OK:
        ESP_LOGI(L_TAG, "Got ssid: %s", (char*)ssid);
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        ESP_LOGI(L_TAG, "SSID is not initialized yet.");
        break;
      default :
        ESP_LOGE(L_TAG, "Error (%s) reading!", esp_err_to_name(err));
    }

    required_size = sizeof(psk);
    err = nvs_get_blob(ee_handle, EE_SAVED_PSK_KEY, psk, &required_size);

    switch (err) {
      case ESP_OK:
        ESP_LOGI(L_TAG, "Got psk: %s", (char*)psk);
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        ESP_LOGI(L_TAG, "PSK is not initialized yet.");
        break;
      default :
        ESP_LOGE(L_TAG, "Error (%s) reading!", esp_err_to_name(err));
    }

    if (strlen((char*)ssid) == 0 || strlen((char*)psk) == 0) {
      ESP_LOGI(L_TAG, "returning false from check for saved ssid/psk");
      return false;
    } else {
      ESP_LOGI(L_TAG, "returning true from check for saved ssid/psk");
      return true;
    }
  }
  ESP_LOGI(L_TAG, "returning false impossibly from check for saved ssid/psk");
  return false;
}


/* save ssid and psk to NVS */
uint8_t ee_save_ssid_psk() {
  ESP_LOGI(L_TAG, "ee_save_ssid_psk():: %s %s", ssid, psk);

  if (ee_is_open == true) {
    ESP_LOGI(L_TAG, "Done");

    // Write SSID
    err = nvs_set_blob(ee_handle, EE_SAVED_SSID_KEY, ssid, sizeof(ssid));

    switch (err) {
      case ESP_OK:
        ESP_LOGI(L_TAG, "Saved ssid: %s", (char*)ssid);
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        ESP_LOGI(L_TAG, "SSID is not found?");
        break;
      default :
        ESP_LOGI(L_TAG, "Error (%s) saving!", esp_err_to_name(err));
    }

    // Write PSK
    err = nvs_set_blob(ee_handle, EE_SAVED_PSK_KEY, psk, sizeof(psk));

    switch (err) {
      case ESP_OK:
        ESP_LOGI(L_TAG, "Saved psk: %s", (char*)psk);
        break;
      default :
        ESP_LOGI(L_TAG, "Error (%s) saving!", esp_err_to_name(err));
    }

    ee_commit();
    return true;
  } else {
    ESP_LOGE(L_TAG, "ESP isn't open to save ssid/psk....");
    return false;
  }
}


uint16_t ee_get_pub_freq() {
  uint16_t pf;
  esp_err_t err = nvs_get_u16(ee_handle, EE_PUB_FREQ_KEY, &pf);
  if (pf < MIN_PUB_FREQ || pf > MAX_PUB_FREQ) {
    ESP_LOGI(L_TAG, "ee_get_pub_freq():: returning default pub freq: %d", EE_DEFAULT_PUB_FREQ);
    //save it for later
    ee_set_pub_freq(EE_DEFAULT_PUB_FREQ);
    return EE_DEFAULT_PUB_FREQ;
  }

  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "Error getting pub_freq value from NVS");
    return EE_DEFAULT_PUB_FREQ;
  } else {
    ESP_LOGI(L_TAG, "ee_get_pub_freq():: %d", pf);
    return pf;
  }
}

uint8_t ee_set_pub_freq(uint16_t new_freq) {
  //set it now
  if (new_freq >= MIN_PUB_FREQ && new_freq <= MAX_PUB_FREQ) {
    //global update
    pub_freq = new_freq;
  }

  //save it for later
  esp_err_t err = nvs_set_u16(ee_handle, EE_PUB_FREQ_KEY, new_freq);
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "Error setting pub_freq value to NVS");
    return false;
  } else {
    ESP_LOGI(L_TAG, "set pub_freq to value: %d in NVS", new_freq);
    ee_commit();
    return true;
  }
}

uint16_t ee_get_ble_listen_duration() {
  uint16_t bld;
  esp_err_t err = nvs_get_u16(ee_handle, EE_BLE_LISTEN_DURATION_KEY, &bld);
  if (bld < MIN_BLE_LISTEN_DURATION || bld > MAX_BLE_LISTEN_DURATION) {
    ESP_LOGI(L_TAG, "ee_get_ble_listen_duration():: returning default ble listen duration freq: %d", EE_BLE_LISTEN_DURATION_KEY);
    //save it for later
    ee_set_ble_listen_duration(EE_DEFAULT_BLE_LISTEN_DURATION);
    return EE_DEFAULT_BLE_LISTEN_DURATION;
  }

  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "Error getting ble_listen_duration value from NVS");
    return EE_DEFAULT_BLE_LISTEN_DURATION;
  } else {
    ESP_LOGI(L_TAG, "ee_get_ble_listen_duration():: %d", bld);
    return bld;
  }
}

uint8_t ee_set_ble_listen_duration(uint16_t new_ble_listen_duration) {
  //set it now
  if (new_ble_listen_duration >= MIN_BLE_LISTEN_DURATION && new_ble_listen_duration <= MAX_BLE_LISTEN_DURATION) {
    //global update
    ble_listen_duration = new_ble_listen_duration;
  }

  //save it for later
  esp_err_t err = nvs_set_u16(ee_handle, EE_BLE_LISTEN_DURATION_KEY, new_ble_listen_duration);
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "Error setting ble_listen_duration value to NVS");
    return false;
  } else {
    ESP_LOGI(L_TAG, "set ble_listen_duration to value: %d in NVS", new_ble_listen_duration);
    ee_commit();
    return true;
  }
}


uint16_t ee_get_engine_detect_samples() {
  uint16_t val;
  esp_err_t err = nvs_get_u16(ee_handle, EE_ENGINE_DETECT_SAMPLES_KEY, &val);
  if (err != ESP_OK || val < MIN_ENGINE_DETECT_SAMPLES || val > MAX_ENGINE_DETECT_SAMPLES) {
    ESP_LOGW(L_TAG, "ee_get_engine_detect_samples: using default (%d)", EE_DEFAULT_ENGINE_DETECT_SAMPLES);
    return EE_DEFAULT_ENGINE_DETECT_SAMPLES;
  }
  ESP_LOGI(L_TAG, "ee_get_engine_detect_samples: %d", val);
  return val;
}

uint8_t ee_set_engine_detect_samples(uint16_t samples) {
  if (samples < MIN_ENGINE_DETECT_SAMPLES || samples > MAX_ENGINE_DETECT_SAMPLES) {
    ESP_LOGE(L_TAG, "ee_set_engine_detect_samples: out of range: %d (valid: %d-%d)",
             samples, MIN_ENGINE_DETECT_SAMPLES, MAX_ENGINE_DETECT_SAMPLES);
    return false;
  }
  esp_err_t err = nvs_set_u16(ee_handle, EE_ENGINE_DETECT_SAMPLES_KEY, samples);
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "ee_set_engine_detect_samples: error saving: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(L_TAG, "ee_set_engine_detect_samples: set to %d", samples);
  ee_commit();
  return true;
}

uint16_t ee_get_engine_mon_interval() {
  uint16_t val;
  esp_err_t err = nvs_get_u16(ee_handle, EE_ENGINE_MON_INTERVAL_KEY, &val);
  if (err != ESP_OK || val < MIN_ENGINE_MON_INTERVAL || val > MAX_ENGINE_MON_INTERVAL) {
    ESP_LOGW(L_TAG, "ee_get_engine_mon_interval: using default (%d)", EE_DEFAULT_ENGINE_MON_INTERVAL);
    return EE_DEFAULT_ENGINE_MON_INTERVAL;
  }
  ESP_LOGI(L_TAG, "ee_get_engine_mon_interval: %d", val);
  return val;
}

uint8_t ee_set_engine_mon_interval(uint16_t minutes) {
  if (minutes < MIN_ENGINE_MON_INTERVAL || minutes > MAX_ENGINE_MON_INTERVAL) {
    ESP_LOGE(L_TAG, "ee_set_engine_mon_interval: out of range: %d (valid: %d-%d)",
             minutes, MIN_ENGINE_MON_INTERVAL, MAX_ENGINE_MON_INTERVAL);
    return false;
  }
  esp_err_t err = nvs_set_u16(ee_handle, EE_ENGINE_MON_INTERVAL_KEY, minutes);
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "ee_set_engine_mon_interval: error saving: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(L_TAG, "ee_set_engine_mon_interval: set to %d", minutes);
  ee_commit();
  return true;
}


uint8_t ee_get_post_flight_upload() {
  uint8_t val = EE_DEFAULT_POST_FLIGHT_UPLOAD;
  esp_err_t err = nvs_get_u8(ee_handle, EE_POST_FLIGHT_UPLOAD_KEY, &val);
  if (err != ESP_OK) {
    ESP_LOGW(L_TAG, "ee_get_post_flight_upload: using default (%d)", EE_DEFAULT_POST_FLIGHT_UPLOAD);
    return EE_DEFAULT_POST_FLIGHT_UPLOAD;
  }
  return val;
}

uint8_t ee_set_post_flight_upload(uint8_t enabled) {
  esp_err_t err = nvs_set_u8(ee_handle, EE_POST_FLIGHT_UPLOAD_KEY, enabled ? ON : OFF);
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "ee_set_post_flight_upload: error saving: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(L_TAG, "ee_set_post_flight_upload: set to %d", enabled ? ON : OFF);
  ee_commit();
  return true;
}


uint8_t ee_get_ota_queued() {
  uint8_t val = OFF;
  nvs_get_u8(ee_handle, EE_OTA_QUEUED_KEY, &val);
  return val;
}

uint8_t ee_set_ota_queued(uint8_t queued) {
  esp_err_t err = nvs_set_u8(ee_handle, EE_OTA_QUEUED_KEY, queued ? ON : OFF);
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "ee_set_ota_queued: error: %s", esp_err_to_name(err));
    return false;
  }
  ee_commit();
  ESP_LOGI(L_TAG, "ee_set_ota_queued: %d", queued ? ON : OFF);
  return true;
}

uint8_t ee_get_upload_queued() {
  uint8_t val = OFF;
  nvs_get_u8(ee_handle, EE_UPLOAD_QUEUED_KEY, &val);
  return val;
}

uint8_t ee_set_upload_queued(uint8_t queued) {
  esp_err_t err = nvs_set_u8(ee_handle, EE_UPLOAD_QUEUED_KEY, queued ? ON : OFF);
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "ee_set_upload_queued: error: %s", esp_err_to_name(err));
    return false;
  }
  ee_commit();
  ESP_LOGI(L_TAG, "ee_set_upload_queued: %d", queued ? ON : OFF);
  return true;
}


uint8_t ee_save_engine_run(uint32_t start_time, uint32_t end_time,
                            float dominant_freq_hz, float peak_magnitude_g,
                            float avg_magnitude_g, uint32_t sample_count) {
  engine_run_t runs[EE_MAX_ENGINE_RUNS];
  size_t blob_size = sizeof(runs);
  uint8_t count = 0;

  esp_err_t err = nvs_get_blob(ee_handle, EE_ENGINE_RUNS_KEY, runs, &blob_size);
  if (err == ESP_OK) {
    count = blob_size / sizeof(engine_run_t);
  } else if (err == ESP_ERR_NVS_NOT_FOUND) {
    count = 0;
  } else {
    ESP_LOGE(L_TAG, "ee_save_engine_run: error reading blob: %s", esp_err_to_name(err));
    return false;
  }

  if (count >= EE_MAX_ENGINE_RUNS) {
    // Drop oldest entry, shift array
    memmove(&runs[0], &runs[1], (EE_MAX_ENGINE_RUNS - 1) * sizeof(engine_run_t));
    count = EE_MAX_ENGINE_RUNS - 1;
  }

  runs[count].start_time       = start_time;
  runs[count].end_time         = end_time;
  runs[count].dominant_freq_hz = dominant_freq_hz;
  runs[count].peak_magnitude_g = peak_magnitude_g;
  runs[count].avg_magnitude_g  = avg_magnitude_g;
  runs[count].sample_count     = sample_count;
  count++;

  err = nvs_set_blob(ee_handle, EE_ENGINE_RUNS_KEY, runs, count * sizeof(engine_run_t));
  if (err != ESP_OK) {
    ESP_LOGE(L_TAG, "ee_save_engine_run: error writing blob: %s", esp_err_to_name(err));
    return false;
  }

  ee_commit();
  ESP_LOGI(L_TAG, "ee_save_engine_run: saved run start=%lu end=%lu freq=%.1fHz peak=%.3fg (total=%d)",
           start_time, end_time, dominant_freq_hz, peak_magnitude_g, count);
  return true;
}


uint8_t ee_clear_engine_runs(void) {
  esp_err_t err = nvs_erase_key(ee_handle, EE_ENGINE_RUNS_KEY);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGE(L_TAG, "ee_clear_engine_runs: error: %s", esp_err_to_name(err));
    return false;
  }
  ee_commit();
  ESP_LOGI(L_TAG, "ee_clear_engine_runs: cleared");
  return true;
}


uint8_t ee_get_engine_runs(engine_run_t *runs, uint8_t *count) {
  size_t blob_size = EE_MAX_ENGINE_RUNS * sizeof(engine_run_t);
  esp_err_t err = nvs_get_blob(ee_handle, EE_ENGINE_RUNS_KEY, runs, &blob_size);
  if (err == ESP_OK) {
    *count = blob_size / sizeof(engine_run_t);
    ESP_LOGI(L_TAG, "ee_get_engine_runs: got %d runs", *count);
    return true;
  } else if (err == ESP_ERR_NVS_NOT_FOUND) {
    *count = 0;
    return true;
  } else {
    ESP_LOGE(L_TAG, "ee_get_engine_runs: error: %s", esp_err_to_name(err));
    *count = 0;
    return false;
  }
}

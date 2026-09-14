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

#ifndef FILE_DEFS_SEEN
#define FILE_DEFS_SEEN

/* Deployment-specific values (server hosts, API paths) live in secretdefs.h,
 * which is not in git. Copy main/secretdefs.h.example over it to build. */
#if defined(__has_include)
#  if !__has_include("secretdefs.h")
#    error "main/secretdefs.h is missing - copy main/secretdefs.h.example to main/secretdefs.h and fill in your server details"
#  endif
#endif
#include "secretdefs.h"


#define RED  5
#define BLU  3
#define GRN  4

/* Firmware version lives in version.txt at the project root and is reported at
 * runtime by fw_version() (util.h), which reads what the bootloader recorded in
 * esp_app_desc_t. Deliberately no VERSION macro here - a second copy of the
 * string could disagree with the image, and the OTA check compares against it. */
#define HW_VERSION "1"

#define ON 1
#define OFF 0
#define YES 1
#define NO 0
#define HI 1
#define LOW 0

#define MQ_CC_MAX_ARG_LEN 30
#define MQ_CC_MAX_PUB_LEN 256


#define LOG_TAG "Hobbsless"
#define TAG "Hobbsless"


/* EE */
#define EE_SER_DEBUG_KEY "ser_debug"
#define EE_DEFAULT_SER_DEBUG OFF

#define EE_VIRGIN_KEY "virgin"
#define EE_INITD_VALUE 0x73

#define EE_SAVED_SSID_KEY "saved_ssid"
#define EE_SAVED_PSK_KEY "saved_psk"

#define EE_PUB_FREQ_KEY "pub_freq"
#define EE_DEFAULT_PUB_FREQ 24 //publish no faster than 24 hours (default)
#define MIN_PUB_FREQ 1
#define MAX_PUB_FREQ 336 // 2 weeks

#define EE_BLE_LISTEN_DURATION_KEY "ble_ld"
#define EE_DEFAULT_BLE_LISTEN_DURATION 60 //listen for 1 minute on ble at first boot (default)
#define MIN_BLE_LISTEN_DURATION 30
#define MAX_BLE_LISTEN_DURATION 600 //10 minutes max, battery

/* HTTP Upload - SERVER_BASE_URL and SERVER_SYNC_PATH are in secretdefs.h */
#define EE_POST_FLIGHT_UPLOAD_KEY      "pfu"
#define EE_DEFAULT_POST_FLIGHT_UPLOAD  ON

#define EE_UPLOAD_QUEUED_KEY           "upl_q"


/* OTA - SERVER_OTA_BASE_URL and its paths are in secretdefs.h */
#define OTA_VERSION_MAX_LEN     32
#define OTA_LATEST_TIMEOUT_MS   10000
#define OTA_DOWNLOAD_TIMEOUT_MS 30000

#define EE_OTA_QUEUED_KEY              "ota_q"


/* SYSTEM STATES FOR LED BLINK */
#define STATE_BOOT       0 // power on (blue)
#define STATE_SC         1 // waiting for smart config (blink red)
#define STATE_NTP        2 // Waiting for NTP to finish (blink yellow)
#define STATE_GOOD       3 // Good and connected (blink green)
#define STATE_FACT       4 // Factory button was depressed (purple)
#define STATE_OTA        5 // Doing OTA download (white)
#define STATE_REBOOTING  6 // Doing OTA download (white)


/* WiFi */
#define ESP_WIFI_MAX_RETRY 3


/* Engine Run Detection */
#define ENGINE_FREQ_THRESHOLD_HZ    15.0f
/* Detection takes N FFT samples 1s apart; a strict majority above the
 * frequency threshold (> N/2) means the engine is running. */
#define EE_ENGINE_DETECT_SAMPLES_KEY    "eds"
#define EE_DEFAULT_ENGINE_DETECT_SAMPLES 10
#define MIN_ENGINE_DETECT_SAMPLES   3
#define MAX_ENGINE_DETECT_SAMPLES   30
#define ENGINE_MONITOR_SAMPLES      5
#define ENGINE_MONITOR_MIN          3    // >=3 of 5 = keep monitoring
#define EE_ENGINE_MON_INTERVAL_KEY  "emi"
#define EE_DEFAULT_ENGINE_MON_INTERVAL  5   // minutes between monitoring checks
#define MIN_ENGINE_MON_INTERVAL     1
#define MAX_ENGINE_MON_INTERVAL     60
#define EE_ENGINE_RUNS_KEY          "eng_runs"
#define EE_MAX_ENGINE_RUNS          50


#endif /* !FILE_DEFS_SEEN */



# SPDX-License-Identifier: Apache-2.0
set(srcs "main.c" "ee.c" "util.c" "ble_cc.c" "wifi.c" "ntp.c" "led.c" "lis3dh.c" "fft.c" "http_upload.c" "ota.c")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "."
                       REQUIRES driver nvs_flash esp_wifi bt esp_netif esp_event esp_http_client esp_https_ota app_update mbedtls)

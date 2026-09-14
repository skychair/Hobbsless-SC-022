/**
 * Hobbsless LED Control
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
#include "led.h"
#include "defs.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const uint8_t leds[] = {RED, GRN, BLU};

void led_init() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RED) | (1ULL << GRN) | (1ULL << BLU),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Start with all LEDs off
    gpio_set_level(RED, ON);
    gpio_set_level(GRN, ON);
    gpio_set_level(BLU, ON);
}

void led_set(uint8_t led, uint8_t state) {
    gpio_set_level(led, state);
}

/* Blink one LED on for ms milliseconds, then off (active low) */
void led_blink(uint8_t led, uint16_t ms) {
    gpio_set_level(led, OFF);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(led, ON);
}

/* Blink yellow (RED + GRN together) on for ms milliseconds, then off */
void led_blink_yellow(uint16_t ms) {
    gpio_set_level(RED, OFF);
    gpio_set_level(GRN, OFF);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(RED, ON);
    gpio_set_level(GRN, ON);
}

/* NTP sync succeeded: RED, GRN, BLU in quick succession, twice (RGBRGB) */
void led_ntp_synced() {
    for (int cycle = 0; cycle < 2; cycle++) {
        for (int i = 0; i < 3; i++) {
            gpio_set_level(leds[i], OFF);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(leds[i], ON);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void led_boot_sequence() {
    // Blink each LED twice (RRGGBB), 80ms on / 80ms off - about 1.1s total
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            gpio_set_level(leds[i], OFF);
            vTaskDelay(pdMS_TO_TICKS(80));
            gpio_set_level(leds[i], ON);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

/* Blink "NO TIME" in Morse code on RED LED at 15 WPM
 * Active-low: led_set(RED, OFF) = on, led_set(RED, ON) = off
 * dit=80ms, dah=240ms, inter-element gap=80ms, inter-char gap=240ms, inter-word gap=560ms
 */
void led_morse_no_time() {
    // N: -. (dah dit)
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(240)); led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(80));
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(80));  led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(240));
    // O: --- (dah dah dah)
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(240)); led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(80));
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(240)); led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(80));
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(240)); led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(560));
    // T: - (dah)
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(240)); led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(240));
    // I: .. (dit dit)
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(80));  led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(80));
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(80));  led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(240));
    // M: -- (dah dah)
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(240)); led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(80));
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(240)); led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(240));
    // E: . (dit)
    led_set(RED, OFF); vTaskDelay(pdMS_TO_TICKS(80));  led_set(RED, ON); vTaskDelay(pdMS_TO_TICKS(240));
}

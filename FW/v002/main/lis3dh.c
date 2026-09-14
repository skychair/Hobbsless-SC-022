/**
 * Hobbsless LIS3DH Accelerometer Driver
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
#include "lis3dh.h"
#include "defs.h"

#include "esp_log.h"
#include "driver/i2c_master.h"

#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

static char L_TAG[] = "LIS3DH";
static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;

esp_err_t lis3dh_init(void) {
    esp_err_t ret;

    // I2C master bus configuration
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = LIS3DH_SDA_PIN,
        .scl_io_num = LIS3DH_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(L_TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add LIS3DH device to the bus
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LIS3DH_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(L_TAG, "I2C add device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify WHO_AM_I
    uint8_t who_am_i;
    ret = lis3dh_read_reg(LIS3DH_REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE(L_TAG, "Failed to read WHO_AM_I");
        return ret;
    }

    if (who_am_i != LIS3DH_WHO_AM_I_VALUE) {
        ESP_LOGE(L_TAG, "WHO_AM_I mismatch: expected 0x%02X, got 0x%02X", LIS3DH_WHO_AM_I_VALUE, who_am_i);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(L_TAG, "WHO_AM_I: 0x%02X (OK)", who_am_i);

    // Configure accelerometer
    // CTRL_REG1: 10Hz ODR, normal mode, all axes enabled
    ret = lis3dh_write_reg(LIS3DH_REG_CTRL_REG1, 0x27);
    if (ret != ESP_OK) return ret;

    // CTRL_REG4: +/- 2g, high resolution
    ret = lis3dh_write_reg(LIS3DH_REG_CTRL_REG4, 0x08);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(L_TAG, "Initialized");

    return ESP_OK;
}

esp_err_t lis3dh_read_reg(uint8_t reg, uint8_t *data) {
    return i2c_master_transmit_receive(
        dev_handle,
        &reg,
        1,
        data,
        1,
        I2C_MASTER_TIMEOUT_MS
    );
}

esp_err_t lis3dh_write_reg(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    return i2c_master_transmit(
        dev_handle,
        buf,
        sizeof(buf),
        I2C_MASTER_TIMEOUT_MS
    );
}

esp_err_t lis3dh_read_accel(int16_t *x, int16_t *y, int16_t *z) {
    esp_err_t ret;
    uint8_t data[6];

    // Read with auto-increment (set MSB of register address)
    uint8_t reg = LIS3DH_REG_OUT_X_L | 0x80;
    ret = i2c_master_transmit_receive(
        dev_handle,
        &reg,
        1,
        data,
        6,
        I2C_MASTER_TIMEOUT_MS
    );

    if (ret != ESP_OK) {
        return ret;
    }

    *x = (int16_t)((data[1] << 8) | data[0]);
    *y = (int16_t)((data[3] << 8) | data[2]);
    *z = (int16_t)((data[5] << 8) | data[4]);

    return ESP_OK;
}

esp_err_t lis3dh_config_interrupt(void) {
    esp_err_t ret;

    // CTRL_REG2: Enable high-pass filter for INT1 (removes static gravity)
    ret = lis3dh_write_reg(LIS3DH_REG_CTRL_REG2, 0x01);
    if (ret != ESP_OK) return ret;

    // CTRL_REG3: Enable INT1 for IA1 (interrupt activity 1)
    ret = lis3dh_write_reg(LIS3DH_REG_CTRL_REG3, 0x40);
    if (ret != ESP_OK) return ret;

    // CTRL_REG5: Latch interrupt on INT1
    ret = lis3dh_write_reg(LIS3DH_REG_CTRL_REG5, 0x08);
    if (ret != ESP_OK) return ret;

    // INT1_THS: Threshold (adjust as needed, ~250mg at 2g scale)
    ret = lis3dh_write_reg(LIS3DH_REG_INT1_THS, 0x10);
    if (ret != ESP_OK) return ret;

    // INT1_DUR: Duration (0 = no duration)
    ret = lis3dh_write_reg(LIS3DH_REG_INT1_DUR, 0x00);
    if (ret != ESP_OK) return ret;

    // INT1_CFG: Enable interrupt on high event on all axes (OR combination)
    ret = lis3dh_write_reg(LIS3DH_REG_INT1_CFG, 0x2A);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(L_TAG, "Interrupt configured on INT1");

    return ESP_OK;
}

esp_err_t lis3dh_clear_interrupt(void) {
    uint8_t src;
    // Reading INT1_SRC clears the interrupt
    return lis3dh_read_reg(LIS3DH_REG_INT1_SRC, &src);
}

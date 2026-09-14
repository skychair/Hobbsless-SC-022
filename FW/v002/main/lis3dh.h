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


#ifndef FILE_LIS3DH_SEEN
#define FILE_LIS3DH_SEEN

#include <stdint.h>
#include "esp_err.h"

#define LIS3DH_I2C_ADDR     0x18  // SA0 to GND (0x19 if SA0 to VCC)

#define LIS3DH_SDA_PIN      8
#define LIS3DH_SCL_PIN      9
#define LIS3DH_INT_PIN      13

// Registers
#define LIS3DH_REG_WHO_AM_I     0x0F
#define LIS3DH_REG_CTRL_REG1    0x20
#define LIS3DH_REG_CTRL_REG2    0x21
#define LIS3DH_REG_CTRL_REG3    0x22
#define LIS3DH_REG_CTRL_REG4    0x23
#define LIS3DH_REG_CTRL_REG5    0x24
#define LIS3DH_REG_CTRL_REG6    0x25
#define LIS3DH_REG_INT1_CFG     0x30
#define LIS3DH_REG_INT1_SRC     0x31
#define LIS3DH_REG_INT1_THS     0x32
#define LIS3DH_REG_INT1_DUR     0x33
#define LIS3DH_REG_OUT_X_L      0x28
#define LIS3DH_REG_OUT_X_H      0x29
#define LIS3DH_REG_OUT_Y_L      0x2A
#define LIS3DH_REG_OUT_Y_H      0x2B
#define LIS3DH_REG_OUT_Z_L      0x2C
#define LIS3DH_REG_OUT_Z_H      0x2D

#define LIS3DH_WHO_AM_I_VALUE   0x33

esp_err_t lis3dh_init(void);
esp_err_t lis3dh_read_reg(uint8_t reg, uint8_t *data);
esp_err_t lis3dh_write_reg(uint8_t reg, uint8_t data);
esp_err_t lis3dh_read_accel(int16_t *x, int16_t *y, int16_t *z);
esp_err_t lis3dh_config_interrupt(void);
esp_err_t lis3dh_clear_interrupt(void);

#endif /* !FILE_LIS3DH_SEEN */

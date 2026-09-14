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

#ifndef FILE_BLE_CC_SEEN
#define FILE_BLE_CC_SEEN

#include <stdint.h>

/**
 * Parse and execute BLE command
 * Command format: C|arg
 * where C is a single character command and arg is the argument
 *
 * @param cmd_str Command string received via BLE SPP
 * @param conn_handle BLE connection handle for sending responses
 * @param attr_handle Attribute handle for notifications
 * @return 1 on success, 0 on failure
 */
uint8_t parse_from_ble(const char *cmd_str, uint16_t conn_handle, uint16_t attr_handle);

#endif /* !FILE_BLE_CC_SEEN */

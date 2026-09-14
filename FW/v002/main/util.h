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

#ifndef FILE_HLUTIL_SEEN
#define FILE_HLUTIL_SEEN

void reboot(uint8_t secs);
void get_mac_uuid(uint8_t *mac_uuid, char *mac_uuid_str);
/* Firmware version as recorded in esp_app_desc_t by the bootloader.
 * Sourced from version.txt at build time - there is no second copy to drift. */
const char *fw_version(void);
int get_epoch();


#endif /* !FILE_HLUTIL_SEEN */

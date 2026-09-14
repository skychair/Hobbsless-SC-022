/**
 * Hobbsless NTP
 *
 * Project Hobbsless
 * Description: SC-022
 * Author: Nick Garner
 * Date: Q4 2025
 *
 * (C) Skychair 2025
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef FILE_NTP_SEEN
#define FILE_NTP_SEEN

uint8_t is_time_valid();
uint8_t sync_sntp_time();
void log_current_time();
uint32_t get_epoch_time();

#endif /* !FILE_NTP_SEEN */

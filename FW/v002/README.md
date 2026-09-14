# Hobbsless SC-022 Firmware

ESP32-S3 battery-optimized IoT device for engine run detection and telemetry logging. Configurable over BLE, syncs time via WiFi/NTP, and uses a LIS3DH accelerometer with FFT analysis to detect and log engine run events during deep sleep wakeups.

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3 (Xtensa dual-core, 240 MHz) |
| BLE Stack | NimBLE (BLE-only) |
| Accelerometer | LIS3DH - I2C (SDA=GPIO8, SCL=GPIO9), INT=GPIO13 |
| LEDs | RED=GPIO5, GRN=GPIO4, BLU=GPIO3 (active-low) |

## Build & Flash

Prerequisites: ESP-IDF environment sourced (`. $IDF_PATH/export.sh`)

**First checkout:** deployment-specific values are not in git. Copy the templates
and fill in your own server before building:

```bash
cp main/secretdefs.h.example main/secretdefs.h
```

`main/secretdefs.h` holds the telemetry host and API path plus the firmware CDN host
and its paths; Building without `secretdefs.h` stops with an `#error` naming the 
file to copy.

```bash
idf.py build
idf.py flash monitor
```

## Boot Flow

```
Every boot (all wakeup paths)
  └─ app_main() prints the device UUID - last 6 hex digits of the MAC

Power-on / Reset
  └─ fresh_boot()
       ├─ LED boot sequence
       ├─ LIS3DH init + interrupt config
       ├─ NVS open, defaults init
       ├─ No WiFi credentials saved → UNCONFIGURED MODE
       │    └─ RED blink every 4s, BLE advertises indefinitely, never sleeps
       ├─ WiFi connect → NTP sync (if creds saved and time invalid)
       └─ BLE advertise (auto-stops after ble_listen_duration seconds)

EXT1 Wakeup (GPIO13 / LIS3DH interrupt)
  └─ woke_by_motion()
       ├─ If time invalid: blink "NO TIME" in Morse (RED LED), open 30s BLE window
       ├─ Phase 1: eds FFT samples × 1s - count samples > 15Hz
       │    └─ Not a strict majority → transient shock → sleep
       └─ Phase 2: Engine detected → monitor every emi minutes (5 FFT samples)
            ├─ <3 samples above threshold → engine stopped → log run to NVS
            └─ If post-flight upload=ON → connect WiFi → HTTPS POST → sleep

Timer Wakeup (pub_freq hours)
  └─ woke_by_timer()
       ├─ Connect WiFi → NTP sync if needed → HTTPS POST pending runs
       ├─ On success: clear NVS runs, apply server commands (pubFreq)
       └─ Refresh next_pub → sleep
```

## Engine Run Detection

When woken by vibration, the device runs a two-phase FFT-based detection:

**Phase 1 - Initial detection** (`eds` samples, default 10, 1 second apart):
- A strict majority (> `eds`/2) of samples with a dominant frequency above 15 Hz → engine detected
- At the default of 10 samples that means ≥6; at 5 samples it would mean ≥3
- Otherwise treated as a transient shock → returns to sleep immediately

**Phase 2 - Run monitoring** (every `emi` minutes, default 5, 5 samples per check):
- While ≥3 of 5 samples remain above 15 Hz → engine still running
- When <3 of 5 samples are above threshold → engine stopped
- Start and end epoch timestamps are saved to NVS (`eng_runs` key, up to 50 records)

FFT parameters: 256 samples @ 400 Hz → ~3.2s window, 1.56 Hz frequency resolution, Hann window.

## NVS Storage

Namespace: `"storage"` - magic byte `0x73` indicates initialized.

### Settings

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `virgin` | u8 | `0x73` | Init magic byte - if not `0x73`, defaults are written |
| `saved_ssid` | blob (33B) | empty | WiFi SSID |
| `saved_psk` | blob (65B) | empty | WiFi passphrase |
| `pub_freq` | u16 | `24` | Upload frequency in hours (1-336) |
| `ble_ld` | u16 | `60` | BLE listen duration in seconds (30-600) |
| `emi` | u16 | `5` | Engine monitor interval in minutes between checks (1-60) |
| `eds` | u16 | `10` | Engine detection FFT samples (3-30) |
| `pfu` | u8 | `1` | Post-flight upload: upload immediately when flight ends (1=ON, 0=OFF) |
| `upl_q` | u8 | `0` | Upload queued: set by `x\|1` BLE command, cleared after upload attempt on next boot |
| `ota_q` | u8 | `0` | OTA check queued: set by `o\|1` BLE command, cleared before the check runs on next boot |

### Engine Run Log

| Key | Type | Purpose |
|-----|------|---------|
| `eng_runs` | blob | Array of up to 50 `engine_run_t` records, oldest-first |

**`engine_run_t` struct** (28 bytes per record):

| Field | Type | Purpose |
|-------|------|---------|
| `start_time` | u32 | Flight start - Unix epoch seconds |
| `end_time` | u32 | Flight end - Unix epoch seconds |
| `dominant_freq_hz` | float | Average dominant vibration frequency across session |
| `peak_magnitude_g` | float | Peak acceleration magnitude seen (g) |
| `avg_magnitude_g` | float | Average acceleration magnitude across session (g) |
| `sample_count` | u32 | Raw accelerometer samples collected (FFT calls × 256) |

**Upload tracking:** All records are uploaded together on the next WiFi connection (either immediately on flight end if `pfu=1`, or at the next `pub_freq` timer wakeup). On successful upload the entire `eng_runs` blob is erased. If upload fails, records are retained for retry. There is no per-record sent/pending flag - it is all-or-nothing.

## BLE Command Protocol

Connect to device advertised as `Hobbsless-XXXXXX`, where `XXXXXX` is the device UUID printed at boot (last 6 hex digits of the WiFi MAC). Service UUID `0xDEAD`, characteristic UUID `0xBEEF` (READ | WRITE | NOTIFY).

**Format**: `C|arg`

| Command | Action | Response |
|---------|--------|----------|
| `v\|1` | Factory reset | ACK byte |
| `r\|1` | Reboot | ACK byte |
| `s\|<ssid>` | Save WiFi SSID | ACK byte |
| `p\|<pass>` | Save WiFi passphrase | ACK byte |
| `c\|1` | Connect WiFi now to test the saved credentials - starts the WiFi stack if it is not already up | ACK byte (1=connected) |
| `f\|<hours>` | Set publish frequency (1-336) | ACK byte |
| `b\|<secs>` | Set BLE listen duration (30-600) | ACK byte |
| `m\|<mins>` | Set engine monitor interval (1-60) | ACK byte |
| `d\|<n>` | Set engine detection FFT samples (3-30) | ACK byte |
| `o\|1` | Check for a firmware update now - queues the check and reboots; runs on next boot once WiFi is up | ACK byte |
| `x\|1` | Force upload now - sets queued flag and reboots; upload runs on next boot after NTP sync | ACK byte |
| `u\|1` | Enable post-flight upload (upload immediately when flight ends) | ACK byte |
| `u\|0` | Disable post-flight upload | ACK byte |
| `g\|pf` | Get publish frequency | Notification |
| `g\|bld` | Get BLE listen duration | Notification |
| `g\|emi` | Get engine monitor interval | Notification |
| `g\|eds` | Get engine detection FFT samples | Notification |
| `g\|s` | Get saved SSID | Notification |
| `g\|p` | Get saved passphrase | Notification |
| `g\|pfu` | Get post-flight upload setting (0 or 1) | Notification |
| `g\|v` | Get firmware version | Notification |

On disconnect, device reboots after 2 seconds to apply new settings.

## LED Indicators

LEDs are **active low** on GPIO5 (RED), GPIO4 (GRN), GPIO3 (BLU): `led_set(x, OFF)`
drives the pin low and lights the LED. All patterns are produced in `led.c`.

Each colour has one meaning, so a glance is unambiguous:

| Pattern | Colour | Timing | Meaning |
|---------|--------|--------|---------|
| Boot sequence | R, G, B in turn | each twice, 80ms on / 80ms off | Fresh power-on (`led_boot_sequence()`) |
| Unconfigured | RED | 80ms blink every 4s, indefinitely | No WiFi credentials saved. Device stays awake and advertising until configured |
| "NO TIME" | RED | Morse `-. --- / - .. -- .` at 15 WPM | Woken by motion but the RTC has no valid time (no prior NTP sync) |
| BLE advertising | BLU | 80ms blink every 5s | Advertising and connectable. Stops the moment a client connects |
| NTP synced | R, G, B then R, G, B | 100ms each, 150ms between the two cycles | SNTP sync succeeded (`led_ntp_synced()`) |
| FFT sample | GRN | 80ms blink per sample | One blink at the start of each FFT sample collection |
| Reboot countdown | RED + GRN (yellow) | 80ms blink each second | Rebooting - one blink per countdown tick, just before each "Restarting in N seconds" log line |
| OTA download | all three (white) | solid for the duration | Image downloading - do not power off |

**Counting green blinks tells you what the device decided.** One blink fires per FFT
sample, immediately before that sample's 256 points are collected:

- A motion wakeup runs a detection cycle of `eds` samples (default 10), so you see 10
  green blinks about 1.6s apart. If fewer than a strict majority are above 15 Hz the
  device treats it as a transient shock and goes straight back to sleep.
- Each subsequent monitor wakeup, every `emi` minutes while an engine is running, is a
  fixed 5 samples - so 5 more green blinks per check until the engine stops.

**Blue means BLE and nothing else.** Earlier firmware also blinked blue once at the
start of a detection or monitor cycle, which was indistinguishable from advertising;
the green per-sample blink replaced it.

## Python BLE Client

```bash
cd ble_client
pip install -r requirements.txt
./hobbsless_client.py
```

Auto-discovers `Hobbsless-*` devices and presents an interactive command menu.

## Source Files

| File | Purpose |
|------|---------|
| `main/main.c` | Boot flow, BLE server, wakeup handlers, sleep orchestration |
| `main/ble_cc.c` | BLE command parser (`C\|arg` format) |
| `main/ee.c` | NVS storage - settings and engine run records |
| `main/wifi.c` | WiFi STA connection manager |
| `main/ntp.c` | SNTP time synchronization |
| `main/lis3dh.c` | LIS3DH accelerometer driver (I2C + interrupt) |
| `main/fft.c` | FFT vibration analysis (256-pt, 400 Hz, esp-dsp) |
| `main/http_upload.c` | HTTPS POST upload of engine run records to server |
| `main/ota.c` | OTA version check and HTTPS firmware download |
| `main/led.c` | LED control including Morse "NO TIME" indicator |
| `main/util.c` | System utilities (reboot, MAC, epoch) |
| `main/defs.h` | All constants and defaults |
| `main/secretdefs.h` | Server hosts and API paths - not in git, see `secretdefs.h.example` |

## OTA Firmware Update

Two 2 MB app slots (`ota_0` / `ota_1`) on the 8 MB flash. Firmware is served from
`SERVER_OTA_BASE_URL`, a separate host from the `SERVER_BASE_URL` sync API; both are
defined in `main/secretdefs.h`. The device checks for an update at every point where it already has WiFi up:

- `fresh_boot()` - after NTP sync and any queued upload
- `try_upload_runs()` - after runs are POSTed, so data is safely away before any reboot

**Versioning.** The firmware version lives in `version.txt` at the project root (currently `2.0.16`) and nowhere else. ESP-IDF embeds it in `esp_app_desc_t`, and `fw_version()` reads it back at runtime, so the string the device compares against `/latest` is by construction the same string that is in the image.

To cut a release:

```bash
echo "2.0.3" > version.txt
idf.py build            # emits build/2.0.3.bin
```

Then upload `build/<version>.bin` to `<SERVER_OTA_IMAGE_PATH><version>.bin` and update `/latest` **last** - a device checking mid-deploy then sees "up to date" rather than a 404. Old versioned binaries accumulate in `build/`; upload the one you just built.

**Protocol:**

1. `GET <SERVER_OTA_BASE_URL><SERVER_OTA_LATEST_PATH>` returns the preferred version identifier. Accepted as a bare string (`1.1.4`), a quoted string, or `{"version":"1.1.4"}`.
2. If that string differs from the version reported by the running image, `GET <SERVER_OTA_BASE_URL><SERVER_OTA_IMAGE_PATH><version>.bin` streams the image into the inactive slot. The `.bin` suffix is appended by the device - `/latest` returns the bare version (`2.0.3`), not a filename.
3. On success the device reboots into the new slot. The comparison is `!=`, not "newer than" - the server decides, so pinning a device to an older build is just a matter of what `/latest` returns.

**Failure handling.** Every step is non-fatal. No server, DNS failure, non-200, empty or unparseable body, truncated download, or failed image validation all log and return, leaving the running firmware untouched and the wake cycle to continue normally. The version string is rejected unless it consists only of `[A-Za-z0-9._-]`, so a malformed response cannot inject a different URL path.

**Rollback.** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is on, so a newly installed image boots as `PENDING_VERIFY`. `ota_mark_valid()` confirms it on reaching `app_main()`; an image that crashes before that point is reverted to the previous slot by the bootloader on the next boot.

## Known TODOs

- BLE `g|runs` command to retrieve stored engine run count/records over BLE

## License

The firmware, BLE clients and scripts are licensed under the Apache License 2.0 - see
`LICENSE`. Each source file carries an `SPDX-License-Identifier: Apache-2.0` tag.

Third-party code keeps its own license:

| Component | License |
|-----------|---------|
| ESP-IDF, NimBLE (fetched at build time) | Apache-2.0 |
| `managed_components/espressif__esp-dsp` | Apache-2.0 |
| `main/ble_spp_server.h` (from the ESP-IDF examples) | Unlicense OR CC0-1.0 |

The license does not grant permission to use the Hobbsless name or marks (Apache-2.0
section 6).

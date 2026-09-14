# Hobbsless Web Configurator

A single-page web app that configures a Hobbsless SC-022 vibration sensor over
Bluetooth, using the Web Bluetooth API. It does everything
`../ble_client/hobbsless_client.py` does, but with a form instead of a numbered menu.

Everything lives in `index.html` - no build step, no dependencies, no server-side code.

## Browser support

Web Bluetooth is required:

| Browser | Supported |
|---|---|
| Chrome / Edge / Opera (desktop, Android) | ✅ |
| Chrome on iOS | ❌ (use [Bluefy](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)) |
| Safari | ❌ |
| Firefox | ❌ |

The page must be loaded from a **secure context** - `https://` or `localhost`.
Opening it as a `file://` URL will not work.

On Linux, Chrome also needs BlueZ running and the experimental platform features
flag in some builds: `chrome://flags/#enable-experimental-web-platform-features`.

## Running it

```bash
cd ble_client_web
python3 -m http.server 8000
```

Then open <http://localhost:8000> (`localhost` counts as a secure context).

To use it from a phone, serve it over HTTPS from any static host - the page is a
single self-contained file.

## Using it

1. Wake the sensor so it starts advertising (it only listens for Bluetooth for
   `ble_listen_duration` seconds after each wake-up, 60 s by default).
2. Click **Connect** and pick your `Hobbsless-XXXXXX` device in the browser prompt.
3. Current settings are read from the sensor and fill the form automatically.
4. Change what you need and click the **Save** button on that card.
5. Click **Disconnect** when done.

Each card saves independently, so you can change Wi-Fi without touching the schedule.

## What the controls map to

| Control | BLE command(s) |
|---|---|
| Save Wi-Fi | `s\|<ssid>` then `p\|<passphrase>` |
| Test connection | `c\|1` |
| Upload every ... hours | `f\|<hours>` (1-336) |
| Engine monitor interval | `m\|<minutes>` (1-60) |
| Upload immediately after each flight | `u\|1` / `u\|0` |
| Vibration samples per check | `d\|<samples>` (3-30) |
| Stay discoverable for ... seconds | `b\|<seconds>` (30-600) |
| Reload settings from sensor | `g\|v`, `g\|pf`, `g\|bld`, `g\|emi`, `g\|eds`, `g\|pfu`, `g\|s`, `g\|p` |
| Upload now | `x\|1` |
| Check for firmware update | `o\|1` |
| Restart sensor | `r\|1` |
| Factory reset | `v\|1` |

Ranges are enforced in the browser and again by the firmware, so an out-of-range
value is caught before it is sent.

## Protocol notes

- Service `0xDEAD`, characteristic `0xBEEF` (READ | WRITE | NOTIFY).
- Commands are UTF-8 `C|arg` strings written to the characteristic.
- **Set** commands reply with a one-byte notification: `1` = success, `0` = failure.
- **Get** commands (`g|*`) reply with a UTF-8 string notification and no ACK byte.
  The app tracks which kind of command is in flight to decode the reply correctly.
- Commands are queued and sent one at a time; overlapping GATT writes are not safe.
- `r|1`, `v|1`, `x|1`, and `o|1` reboot the device from inside the command handler, so the
  firmware never gets to send an ACK. The app fires those without waiting for a reply.
- The sensor reboots on disconnect to apply saved settings, and then sleeps until its
  next wake-up. Expect to have to wake it again before reconnecting.

## Troubleshooting

**No devices in the chooser** - the sensor's Bluetooth window has probably expired.
Wake it and click Connect again. Also confirm Bluetooth is on: `bluetoothctl power on`.

**Connects then immediately drops** - another client (the Python client, `bluetoothctl`)
may still hold the connection. Only one client can be connected at a time.

**A setting reads back blank** - the field has never been set on the device. Empty
strings come back as zero-length notifications, which some Bluetooth stacks drop;
the app logs the read failure and moves on rather than stalling.

Open the **Activity log** at the bottom of the page to see every command sent and
every reply received.

# Hobbsless BLE Client

Python BLE client for connecting to and controlling Hobbsless devices.

## Requirements

- Linux with Bluetooth support
- Python 3.7+
- BlueZ (Linux Bluetooth stack)

## Installation

1. Install Python dependencies:
```bash
pip install -r requirements.txt
```

Or manually:
```bash
pip install bleak
```

2. Make the script executable:
```bash
chmod +x hobbsless_client.py
```

## Usage

Run the client:
```bash
./hobbsless_client.py
```

Or:
```bash
python3 hobbsless_client.py
```

## Features

- **Auto-discovery**: Scans for devices with names starting with "Hobbsless"
- **Multiple device support**: If multiple devices found, prompts to select one
- **Interactive menu**: Shows all available commands
- **Response display**: Shows device responses (data or ACK)
- **Custom commands**: Option to send custom commands

## Available Commands

| Option | Command | Description |
|--------|---------|-------------|
| 1 | `v\|1` | Factory reset (erase NVS and reboot) |
| 2 | `r\|1` | Reboot device |
| 3 | `s\|<ssid>` | Save WiFi SSID |
| 4 | `p\|<pass>` | Save WiFi passphrase |
| 5 | `f\|<hours>` | Set pub_freq (1-336 hours) |
| 6 | `b\|<secs>` | Set BLE duration (30-600 seconds) |
| 7 | `g\|pf` | Get pub_freq |
| 8 | `g\|bld` | Get BLE listen duration |
| 9 | `g\|s` | Get WiFi SSID |
| 10 | `g\|p` | Get WiFi passphrase |
| 11 | Custom | Send custom command |
| q | - | Quit |

## Example Session

```
Scanning for Hobbsless devices...
  Found: Hobbsless-6F4E (AA:BB:CC:DD:EE:FF)

Connecting to Hobbsless-6F4E...
Connected to Hobbsless-6F4E!
Notifications enabled.

============================================================
HOBBSLESS BLE COMMAND & CONTROL
============================================================

Opt  Command              Description
------------------------------------------------------------
1    v|1                  Factory reset (erase NVS and reboot)
2    r|1                  Reboot device
3    s|<value>            Save WiFi SSID
...

Enter option: 7
>>> Sending: g|pf
<<< Response: 24

Enter option: 3
Enter value for Save WiFi SSID: MyNetwork
>>> Sending: s|MyNetwork
<<< ACK: 1 (Success)
```

## Troubleshooting

### Permission Denied
If you get permission errors on Linux:
```bash
sudo setcap cap_net_raw+eip $(which python3)
```

Or run with sudo:
```bash
sudo python3 hobbsless_client.py
```

### No Devices Found
- Make sure the Hobbsless device is powered on
- Ensure Bluetooth is enabled on your Linux system: `bluetoothctl power on`
- Check if the device is advertising: `bluetoothctl scan on`

### Connection Failed
- Device might be out of range
- Bluetooth might be blocked by another application
- Try restarting Bluetooth: `sudo systemctl restart bluetooth`

## Notes

- Device will reboot after disconnecting to apply new settings
- Get commands (`g|*`) return string data
- Set commands return ACK byte (1=success, 0=failure)

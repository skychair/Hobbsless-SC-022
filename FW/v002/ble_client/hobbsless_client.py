#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# (C) Skychair 2026
"""
Hobbsless BLE Client
Connects to Hobbsless devices and sends commands via BLE
"""

import asyncio
import sys
from bleak import BleakScanner, BleakClient

# UUIDs for Hobbsless SPP service
SPP_SERVICE_UUID = "0000dead-0000-1000-8000-00805f9b34fb"
SPP_CHAR_UUID = "0000beef-0000-1000-8000-00805f9b34fb"

# Command reference table
COMMANDS = {
    "1": {"cmd": "v|1", "desc": "Factory reset (erase NVS and reboot)"},
    "2": {"cmd": "r|1", "desc": "Reboot device"},
    "3": {"cmd": "s|", "desc": "Save WiFi SSID", "needs_arg": True},
    "4": {"cmd": "p|", "desc": "Save WiFi passphrase", "needs_arg": True},
    "5": {"cmd": "c|1", "desc": "Connect to WiFi (using saved credentials)"},
    "6": {"cmd": "f|", "desc": "Set pub_freq (hours, 1-336)", "needs_arg": True},
    "7": {"cmd": "b|", "desc": "Set BLE duration (seconds, 30-600)", "needs_arg": True},
    "8": {"cmd": "g|pf", "desc": "Get pub_freq"},
    "9": {"cmd": "g|bld", "desc": "Get BLE listen duration"},
    "10": {"cmd": "g|s", "desc": "Get WiFi SSID"},
    "11": {"cmd": "g|p", "desc": "Get WiFi passphrase"},
    "12": {"cmd": "g|pfu", "desc": "Get post-flight upload setting"},
    "13": {"cmd": "u|", "desc": "Set post-flight upload (1=on, 0=off)", "needs_arg": True},
    "14": {"cmd": "g|v", "desc": "Get firmware version"},
    "15": {"cmd": "x|1", "desc": "Force upload now (queues upload and reboots)"},
    "16": {"cmd": "g|emi", "desc": "Get engine monitor interval (minutes)"},
    "17": {"cmd": "m|", "desc": "Set engine monitor interval in minutes (1-60)", "needs_arg": True},
    "18": {"cmd": "g|eds", "desc": "Get engine detection FFT samples"},
    "19": {"cmd": "d|", "desc": "Set engine detection FFT samples (3-30)", "needs_arg": True},
    "20": {"cmd": "o|1", "desc": "Check for firmware update now (queues check and reboots)"},
    "21": {"cmd": "", "desc": "Send custom command", "custom": True},
    "0": {"cmd": "", "desc": "Get all settings (run all get commands)", "get_all": True},
}

class HobbslessClient:
    def __init__(self):
        self.client = None
        self.response_received = False
        self.last_response = None

    async def find_device(self):
        """Scan for Hobbsless devices"""
        print("Scanning for Hobbsless devices...")
        devices = await BleakScanner.discover(timeout=5.0)

        hobbsless_devices = []
        for device in devices:
            if device.name and device.name.startswith("Hobbsless"):
                hobbsless_devices.append(device)
                print(f"  Found: {device.name} ({device.address})")

        if not hobbsless_devices:
            print("No Hobbsless devices found!")
            return None

        if len(hobbsless_devices) == 1:
            return hobbsless_devices[0]

        # Multiple devices found, let user choose
        print("\nMultiple devices found. Select one:")
        for idx, dev in enumerate(hobbsless_devices):
            print(f"  {idx + 1}. {dev.name} ({dev.address})")

        while True:
            try:
                choice = int(input("Enter device number: "))
                if 1 <= choice <= len(hobbsless_devices):
                    return hobbsless_devices[choice - 1]
            except (ValueError, KeyboardInterrupt):
                pass
            print("Invalid choice, try again.")

    def notification_handler(self, sender, data):
        """Handle notifications from the device"""
        try:
            # Try to decode as string
            response = data.decode('utf-8')
            print(f"\n<<< Response: {response}")
        except UnicodeDecodeError:
            # If not a string, show as bytes
            if len(data) == 1:
                print(f"\n<<< ACK: {data[0]} ({'Success' if data[0] == 1 else 'Failure'})")
            else:
                print(f"\n<<< Response (hex): {data.hex()}")

        self.last_response = data
        self.response_received = True

    def print_menu(self):
        """Print the command menu"""
        print("\n" + "="*60)
        print("HOBBSLESS BLE COMMAND & CONTROL")
        print("="*60)
        print("\n{:<4} {:<20} {}".format("Opt", "Command", "Description"))
        print("-"*60)

        # Show option 0 first (get all)
        if "0" in COMMANDS:
            cmd_info = COMMANDS["0"]
            print("{:<4} {:<20} {}".format("0", "[GET ALL]", cmd_info["desc"]))
            print("-"*60)

        # Show remaining options in order
        for key in sorted(COMMANDS.keys(), key=lambda x: int(x) if x.isdigit() else 999):
            if key == "0":  # Already printed
                continue
            cmd_info = COMMANDS[key]
            cmd_display = cmd_info["cmd"] if not cmd_info.get("needs_arg") else cmd_info["cmd"] + "<value>"
            print("{:<4} {:<20} {}".format(key, cmd_display, cmd_info["desc"]))

        print("-"*60)
        print("  q - Quit")
        print("="*60)

    async def send_command(self, command_str):
        """Send a command to the device"""
        if not self.client or not self.client.is_connected:
            print("Not connected to device!")
            return False

        try:
            # Write the command
            print(f">>> Sending: {command_str}")
            await self.client.write_gatt_char(
                SPP_CHAR_UUID,
                command_str.encode('utf-8'),
                response=False
            )

            # Wait for response (with timeout)
            self.response_received = False
            timeout = 2.0
            elapsed = 0.0
            while not self.response_received and elapsed < timeout:
                await asyncio.sleep(0.1)
                elapsed += 0.1

            if not self.response_received:
                print("No response received (timeout)")

            return True

        except Exception as e:
            print(f"Error sending command: {e}")
            return False

    async def run(self):
        """Main application loop"""
        # Find device
        device = await self.find_device()
        if not device:
            return

        print(f"\nConnecting to {device.name}...")

        try:
            async with BleakClient(device.address) as client:
                self.client = client
                print(f"Connected to {device.name}!")

                # List all services and characteristics for debugging
                print("\n--- Available Services ---")
                for service in client.services:
                    print(f"Service: {service.uuid}")
                    for char in service.characteristics:
                        props = ','.join(char.properties)
                        print(f"  Char: {char.uuid} [{props}]")
                        if char.uuid.lower() == SPP_CHAR_UUID.lower():
                            print(f"    ^^^ FOUND SPP CHARACTERISTIC!")

                # Enable notifications
                print(f"\nEnabling notifications on {SPP_CHAR_UUID}...")
                await client.start_notify(SPP_CHAR_UUID, self.notification_handler)
                print("Notifications enabled successfully.")

                # Main command loop
                while True:
                    self.print_menu()

                    choice = input("\nEnter option: ").strip()

                    if choice.lower() == 'q':
                        print("Disconnecting...")
                        break

                    if choice not in COMMANDS:
                        print("Invalid option!")
                        continue

                    cmd_info = COMMANDS[choice]

                    # Handle get all settings
                    if cmd_info.get("get_all"):
                        print("\n" + "="*60)
                        print("GETTING ALL SETTINGS")
                        print("="*60)

                        get_commands = [
                            ("Firmware Version", "g|v"),
                            ("Pub Freq (hours)", "g|pf"),
                            ("BLE Listen Duration (sec)", "g|bld"),
                            ("Post-Flight Upload", "g|pfu"),
                            ("Engine Monitor Interval (min)", "g|emi"),
                            ("Engine Detection Samples", "g|eds"),
                            ("WiFi SSID", "g|s"),
                            ("WiFi Passphrase", "g|p"),
                        ]

                        for label, cmd in get_commands:
                            print(f"\n{label}:")
                            await self.send_command(cmd)
                            await asyncio.sleep(0.3)  # Brief pause between commands

                        print("\n" + "="*60)
                        input("\nPress Enter to continue...")
                        continue

                    # Handle custom command
                    if cmd_info.get("custom"):
                        command_str = input("Enter custom command (e.g., 'g|pf'): ").strip()
                    # Handle commands that need arguments
                    elif cmd_info.get("needs_arg"):
                        arg = input(f"Enter value for {cmd_info['desc']}: ").strip()
                        command_str = cmd_info["cmd"] + arg
                    else:
                        command_str = cmd_info["cmd"]

                    if not command_str:
                        print("Empty command, skipping.")
                        continue

                    await self.send_command(command_str)
                    await asyncio.sleep(0.5)  # Brief pause before showing menu again

                # Disable notifications before disconnect
                await client.stop_notify(SPP_CHAR_UUID)

        except Exception as e:
            print(f"Error: {e}")
        finally:
            self.client = None
            print("Disconnected.")

async def main():
    client = HobbslessClient()
    await client.run()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        sys.exit(0)

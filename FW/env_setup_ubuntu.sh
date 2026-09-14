#!/bin/bash


# Install deps
echo "sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0"
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0



# Clone ESP-IDF
echo "mkdir -p ~/esp552"
mkdir -p ~/esp552
echo "cd ~/esp552"
cd ~/esp552
echo "git clone -b v5.5.2 --recursive https://github.com/espressif/esp-idf.git"
git clone -b v5.5.2 --recursive https://github.com/espressif/esp-idf.git



# Set up the tools
cd ~/esp552/esp-idf
./install.sh esp32s3



# Env
echo "You'll need environment variables to run, either:"
echo "1. run:"
echo "    . $HOME/esp552/esp-idf/export.sh"
echo "2. set an alias in your shell init script:"
echo "    alias idf552='. $HOME/esp552/esp-idf/export.sh'"
echo "    and then run the alias in your shell to set the variables"

echo "3. Ensure the user is in the uucp and dialout groups:"
echo "     sudo usermod -aG uucp,dialout $USER"


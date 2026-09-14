#!/bin/bash


# Install deps
echo "sudo pacman -S --needed gcc git make flex bison gperf python cmake ninja ccache dfu-util libusb python-pip"
sudo pacman -S --needed gcc git make flex bison gperf python cmake ninja ccache dfu-util libusb python-pip



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


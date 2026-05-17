#!/bin/bash
# set -e

PORT=$(usbipd list | grep "CP210x|CH340" | head -1 | awk '{print $1}')
if ls /dev/ttyUSB* 1> /dev/null 2>&1; then
    echo "Микроконтроллер подключен"
else
    echo "НЕ подключен"
    usbipd bind --busid $PORT
    usbipd attach --wsl --busid $PORT
fi

conda activate pio
source /home/tru60/biochemlab/biochemlab/bin/activate

rm -rfv .pio/build/esp32dev/src/*.o
platformio run --target upload
# pio device monitor --baud 115200 
# poetry run esptool --chip esp32 --port /dev/ttyUSB0 erase_region 0x10000 0x20000 > /dev/null 2>&1 &

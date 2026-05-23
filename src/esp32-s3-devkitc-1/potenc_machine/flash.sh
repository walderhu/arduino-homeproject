

usbipd list
usbipd bind --busid 1-2
usbipd attach --wsl --busid 1-2

lsusb
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null


platformio run --target upload
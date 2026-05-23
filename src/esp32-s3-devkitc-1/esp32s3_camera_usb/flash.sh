

usbipd list
usbipd bind --busid 1-2
usbipd attach --wsl --busid 1-2

lsusb
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null


python3 -m platformio run --target upload


# python3 -m pip install pyserial opencv-python numpy



usbipd list
usbipd bind --busid 1-2
usbipd attach --wsl --busid 1-2

lsusb
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
python3 viewer.py /dev/ttyACM0
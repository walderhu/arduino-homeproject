# arduino-homeproject

Набор отдельных PlatformIO-проектов для ESP32/ESP32-C3/ESP32-S3. Каждый проект живет в своей папке с `platformio.ini`, поэтому его можно собирать и прошивать независимо.

## Быстрый старт

```bash
cd /path/to/arduino-homeproject
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

Проверка:

```bash
python -m platformio --version
```

## Прошивка

Общий скрипт:

```bash
./src/flash.sh <path-to-project-or-file>
```

Можно передать директорию проекта, `platformio.ini` или `src/main.ino`:

```bash
./src/flash.sh src/controllers/radiomaster_tx12
./src/flash.sh src/controllers/radiomaster_pocket/platformio.ini
./src/flash.sh src/esp32-s3-devkitc-1/esp32s3_camera_usb/src/main.ino
```

Скрипт поднимается к ближайшему `platformio.ini`, запускает `platformio run --target upload`, затем открывает serial monitor.

## Текущая структура

```text
src/
  controllers/
    display_joystick/
    radiomaster_pocket/
    radiomaster_tx12/

  esp32dev/
    camera_0v7670/
    display/
      display1602/
      display_thermal/
    home_project/
    machine/
      battery/
      stepper/
    radio/
      NRF24L01/

  esp32-c3-super-mini/
    display_joystick/

  esp32-s3-devkitc-1/
    esp32s3_camera_stream/
    esp32s3_camera_usb/
    esp32s3_camera_webserver/
    potenc_machine/

  ESPNOW_esp32-c3-super-mini_with_esp32-s3-devkitc-1_display_joystick_motor_system/
    c3-master/
    s3-slave/
```

## Controllers

- `src/controllers/radiomaster_tx12` читает стики и переключатели RadioMaster TX12 через CRSF external module bay.
- `src/controllers/radiomaster_pocket` читает стики, переключатели и `S1` RadioMaster Pocket через CRSF nano module bay.
- `src/controllers/display_joystick` старый локальный joystick/display проект.

## Даташиты

Проектные изображения и распиновки лежат рядом с прошивкой в `datasheet/`. Общие изображения лежат в `misc/imgs/datasheets/` и могут подключаться симлинками.

## Локальный мусор

В git не добавляются:

- `.pio/`
- `.venv/`, `env/`
- `config.h`
- вложенные `.git/`, `.codex/`, `.agents/`
- `*.backup-*`

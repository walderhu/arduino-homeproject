#!/bin/bash
# set -e

find_project_dir() {
    local path="$1"

    if [ -z "$path" ]; then
        path="."
    fi

    if [ -f "$path" ]; then
        path="$(dirname "$path")"
    fi 

    path="$(cd "$path" && pwd)"

    while [ "$path" != "/" ]; do
        if [ -f "$path/platformio.ini" ]; then
            printf '%s\n' "$path"
            return 0
        fi
        path="$(dirname "$path")"
    done

    return 1
}

PROJECT_DIR="$(find_project_dir "${1:-.}")"
if [ -z "$PROJECT_DIR" ]; then
    echo "Не найден platformio.ini для: ${1:-.}" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VENV_DIR="${ARDUINO_HOMEPROJECT_VENV:-$REPO_ROOT/.venv}"
PLATFORMIO="$VENV_DIR/bin/platformio"
PIO="$VENV_DIR/bin/pio"

if [ ! -x "$PLATFORMIO" ] || [ ! -x "$PIO" ]; then
    echo "Не найден PlatformIO в $VENV_DIR" >&2
    echo "Создайте окружение: python3 -m venv .venv && .venv/bin/pip install -r requirements.txt" >&2
    exit 1
fi

if grep -qiE '(microsoft|wsl)' /proc/version 2>/dev/null; then
    USBIPD="usbipd.exe"
    if ! command -v "$USBIPD" >/dev/null 2>&1; then
        echo "Не найден usbipd.exe. Запускайте из WSL с доступным usbipd-win из Windows PATH." >&2
        exit 1
    fi

    PORT=$("$USBIPD" list 2>/dev/null | tr -d '\r' | awk '/CP210x|CH340/ && $1 ~ /^[0-9]+-[0-9]+$/ { print $1; exit }')
    if ls /dev/ttyUSB* 1> /dev/null 2>&1; then
        echo "Микроконтроллер подключен"
    else
        echo "НЕ подключен"
        if [ -z "$PORT" ]; then
            echo "Не найден подключенный CP210x/CH340 в usbipd.exe list" >&2
            exit 1
        fi

        "$USBIPD" bind --busid "$PORT" || exit 1
        "$USBIPD" attach --wsl --busid "$PORT" || exit 1
    fi
fi

cd "$PROJECT_DIR"

rm -rfv .pio/build/esp32dev/src/*.o
# pio run --target clean
"$PLATFORMIO" run --target upload
"$PIO" device monitor --baud 115200
# poetry run esptool --chip esp32 --port /dev/ttyUSB0 erase_region 0x10000 0x20000 > /dev/null 2>&1 &

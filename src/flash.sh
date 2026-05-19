#!/usr/bin/env bash
set -euo pipefail

find_project_dir() {
    local path="${1:-.}"

    if [ "$(basename "$path")" = "platformio.ini" ]; then
        path="$(dirname "$path")"
    elif [ -f "$path" ]; then
        path="$(dirname "$path")"
    elif [ ! -e "$path" ] && [ -d "$(dirname "$path")" ]; then
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

wait_for_ttyusb() {
    local timeout_sec="${1:-10}"
    local elapsed=0

    while [ "$elapsed" -lt "$timeout_sec" ]; do
        if ls /dev/ttyUSB* >/dev/null 2>&1; then
            return 0
        fi

        sleep 1
        elapsed=$((elapsed + 1))
    done

    return 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PROJECT_DIR="$(find_project_dir "${1:-.}")"

if [ -z "$PROJECT_DIR" ]; then
    echo "Не найден platformio.ini для: ${1:-.}" >&2
    exit 1
fi

VENV_DIR="${ESP32_LEVEL_VENV:-$REPO_ROOT/.venv}"
PYTHON="$VENV_DIR/bin/python"

if [ ! -x "$PYTHON" ]; then
    echo "Не найден PlatformIO в $VENV_DIR" >&2
    echo "Создайте окружение:" >&2
    echo "  python3 -m venv .venv" >&2
    echo "  .venv/bin/python -m pip install -r requirements.txt" >&2
    exit 1
fi

if ! "$PYTHON" -m platformio --version >/dev/null 2>&1; then
    echo "PlatformIO не установлен в $VENV_DIR" >&2
    echo "Установите зависимости:" >&2
    echo "  $PYTHON -m pip install -r requirements.txt" >&2
    exit 1
fi

if grep -qiE '(microsoft|wsl)' /proc/version 2>/dev/null; then
    USBIPD="${USBIPD_EXE:-usbipd.exe}"

    if ! command -v "$USBIPD" >/dev/null 2>&1; then
        echo "Ошибка: usbipd.exe не найден." >&2
        exit 1
    fi

    if ! ls /dev/ttyUSB* >/dev/null 2>&1; then
        PORT="$("$USBIPD" list 2>/dev/null | tr -d '\r' | awk '/CP210x|CH340/ && $1 ~ /^[0-9]+-[0-9]+$/ { print $1; exit }')"

        if [ -z "$PORT" ]; then
            echo "Ошибка: ESP32 не подключена (CP210x/CH340 не найден)." >&2
            exit 1
        fi

        "$USBIPD" bind --busid "$PORT" || true
        "$USBIPD" attach --wsl --busid "$PORT" || true

        if ! wait_for_ttyusb 10; then
            echo "Ошибка: устройство не появилось в /dev/ttyUSB* после attach." >&2
            exit 1
        fi
    fi
fi

cd "$PROJECT_DIR"
"$PYTHON" -m platformio run --target upload
"$PYTHON" -m platformio device monitor --baud 115200

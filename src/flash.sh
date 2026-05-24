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

find_serial_device() {
    local device

    for device in /dev/ttyACM* /dev/ttyUSB*; do
        if [ -e "$device" ]; then
            printf '%s\n' "$device"
            return 0
        fi
    done

    return 1
}

wait_for_serial_device() {
    local timeout_sec="${1:-10}"
    local elapsed=0

    while [ "$elapsed" -lt "$timeout_sec" ]; do
        if find_serial_device >/dev/null; then
            return 0
        fi

        sleep 1
        elapsed=$((elapsed + 1))
    done

    return 1
}

print_usbipd_connected() {
    local usbipd="$1"

    "$usbipd" list | tr -d '\r' | sed '/^Persisted:/,$d'
}

print_usbipd_connected_rows() {
    local usbipd="$1"

    print_usbipd_connected "$usbipd" | awk '$1 ~ /^[0-9]+-[0-9]+$/ { print }'
}

save_usbipd_baseline() {
    local usbipd="$1"

    if [ -n "${USBIPD_BASELINE_FILE:-}" ]; then
        print_usbipd_connected_rows "$usbipd" >"$USBIPD_BASELINE_FILE"
    fi

    return 0
}

print_usbipd_added_devices() {
    local usbipd="$1"
    local before
    local after
    local added

    before="$(mktemp)"
    after="$(mktemp)"

    if [ -n "${USBIPD_BASELINE_FILE:-}" ] && [ -s "$USBIPD_BASELINE_FILE" ]; then
        cp "$USBIPD_BASELINE_FILE" "$before"
    else
        echo "Baseline usbipd не найден. Отключите ESP32/USB-устройство и нажмите Enter."
        read -r
        print_usbipd_connected_rows "$usbipd" >"$before"

        echo "Подключите ESP32/USB-устройство и нажмите Enter."
        read -r
    fi

    print_usbipd_connected_rows "$usbipd" >"$after"

    added="$(grep -Fvx -f "$before" "$after" || true)"
    rm -f "$before" "$after"

    if [ -z "$added" ]; then
        echo "Новых USB-устройств не найдено."
        return 1
    fi

    echo "Новые USB-устройства:"
    printf '%s\n' "$added"
}

recapture_usbipd_added_devices() {
    local usbipd="$1"

    echo "Отключите ESP32/USB-устройство и нажмите Enter."
    read -r
    save_usbipd_baseline "$usbipd"

    echo "Подключите ESP32/USB-устройство и нажмите Enter."
    read -r
    print_usbipd_added_devices "$usbipd"
}

get_usbipd_added_busids() {
    local usbipd="$1"
    local before
    local after

    if [ -z "${USBIPD_BASELINE_FILE:-}" ] || [ ! -s "$USBIPD_BASELINE_FILE" ]; then
        return 1
    fi

    before="$(mktemp)"
    after="$(mktemp)"

    cp "$USBIPD_BASELINE_FILE" "$before"
    print_usbipd_connected_rows "$usbipd" >"$after"
    grep -Fvx -f "$before" "$after" | awk '{ print $1 }' || true
    rm -f "$before" "$after"
}

normalize_usbipd_busid() {
    local usbipd="$1"
    local input="$2"
    local busid

    if [[ "$input" =~ ^[0-9]+-[0-9]+$ ]]; then
        printf '%s\n' "$input"
        return 0
    fi

    if [[ ! "$input" =~ ^[0-9]+$ ]]; then
        return 1
    fi

    busid="$(print_usbipd_connected_rows "$usbipd" | awk -v suffix="-$input" '$1 ~ suffix "$" { print $1; exit }')"

    if [ -z "$busid" ]; then
        return 1
    fi

    printf '%s\n' "$busid"
}

attach_usbipd_busid() {
    local usbipd="$1"
    local busid="$2"

    echo "Подключаю USB-устройство $busid в WSL..."
    "$usbipd" bind --busid "$busid" || true
    "$usbipd" attach --wsl --busid "$busid" || true

    if ! wait_for_serial_device 10; then
        echo "Ошибка: устройство не появилось в /dev/ttyACM* или /dev/ttyUSB* после attach." >&2
        return 1
    fi

    save_usbipd_baseline "$usbipd"
}

attach_usbipd_device_interactive() {
    local usbipd="$1"
    local busid
    local added_busids
    local added_count

    echo "ESP32 не найдена в /dev/ttyACM* или /dev/ttyUSB*."

    added_busids="$(get_usbipd_added_busids "$usbipd" || true)"
    added_count="$(printf '%s\n' "$added_busids" | awk 'NF { count++ } END { print count + 0 }')"

    if [ "$added_count" -eq 1 ]; then
        busid="$added_busids"
        echo "Найдено новое USB-устройство: $busid"

        if attach_usbipd_busid "$usbipd" "$busid"; then
            return 0
        fi

        echo "Автоподключение не сработало, показываю список вручную." >&2
    elif [ "$added_count" -gt 1 ]; then
        echo "Найдено несколько новых USB-устройств:"
        printf '%s\n' "$added_busids"
        echo
    fi

    echo "Доступные USB-устройства Windows:"
    print_usbipd_connected "$usbipd"

    if [ "$added_count" -ne 1 ] && [ -n "${USBIPD_BASELINE_FILE:-}" ] && [ -s "$USBIPD_BASELINE_FILE" ]; then
        echo
        print_usbipd_added_devices "$usbipd" || true
    fi

    echo
    read -r -p "Введите номер USB (например 2 для 1-2), полный BUSID, d для diff, или q/любую букву для выхода: " busid

    if [[ "$busid" =~ ^[dD]$ ]]; then
        recapture_usbipd_added_devices "$usbipd" || exit 1
        echo
        read -r -p "Введите номер USB из diff (например 2 для 1-2), полный BUSID, или q/любую букву для выхода: " busid
    fi

    if ! busid="$(normalize_usbipd_busid "$usbipd" "$busid")"; then
        echo "Выход: устройство не выбрано." >&2
        exit 1
    fi

    if ! attach_usbipd_busid "$usbipd" "$busid"; then
        exit 1
    fi
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_DIR="$(find_project_dir "${1:-.}")"

if [ -z "$PROJECT_DIR" ]; then
    echo "Не найден platformio.ini для: ${1:-.}" >&2
    exit 1
fi

VENV_DIR="${ESP32_LEVEL_VENV:-$REPO_ROOT/.venv}"
PYTHON="$VENV_DIR/bin/python"
UPLOAD_PORT="${UPLOAD_PORT:-}"

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

    if ! UPLOAD_PORT="$(find_serial_device)"; then
        attach_usbipd_device_interactive "$USBIPD"
    fi
fi

if [ -z "$UPLOAD_PORT" ]; then
    UPLOAD_PORT="$(find_serial_device || true)"
fi

cd "$PROJECT_DIR"
"$PYTHON" -m platformio run --target upload ${UPLOAD_PORT:+--upload-port "$UPLOAD_PORT"}
"$PYTHON" -m platformio device monitor --baud 115200 ${UPLOAD_PORT:+--port "$UPLOAD_PORT"}

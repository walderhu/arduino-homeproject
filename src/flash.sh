set -euo pipefail
#!/usr/bin/env bash

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

list_project_dirs() {
    find "$SCRIPT_DIR" -name platformio.ini \
        ! -path '*/.pio/*' \
        ! -path '*/build/*' \
        -print \
        | sed 's#/platformio\.ini$##' \
        | sort
}

choose_from_list() {
    local prompt="$1"
    shift
    local items=("$@")
    local choice

    if [ "${#items[@]}" -eq 0 ]; then
        return 1
    fi

    if [ "${#items[@]}" -eq 1 ]; then
        printf '%s\n' "${items[0]}"
        return 0
    fi

    printf '%s\n' "$prompt" >&2
    for i in "${!items[@]}"; do
        printf '  %d) %s\n' "$((i + 1))" "${items[$i]}" >&2
    done

    while true; do
        read -r -p '> ' choice
        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#items[@]}" ]; then
            printf '%s\n' "${items[$((choice - 1))]}"
            return 0
        fi
        echo "Введите номер от 1 до ${#items[@]}" >&2
    done
}

choose_usbipd_busid() {
    local choice
    local busid

    printf '%s\n' "Какую USB-плату подключить:" >&2
    for i in "${!USBIPD_LINES[@]}"; do
        busid="$(awk '{ print $1 }' <<< "${USBIPD_LINES[$i]}")"
        printf '  %d) %s  %s\n' "$((i + 1))" "$busid" "${USBIPD_LINES[$i]#"$busid"}" >&2
    done

    while true; do
        read -r -p '> ' choice

        if [[ "$choice" =~ ^[0-9]+-[0-9]+$ ]]; then
            for line in "${USBIPD_LINES[@]}"; do
                busid="$(awk '{ print $1 }' <<< "$line")"
                if [ "$choice" = "$busid" ]; then
                    printf '%s\n' "$line"
                    return 0
                fi
            done
        fi

        if [[ "$choice" =~ ^[0-9]+$ ]]; then
            for line in "${USBIPD_LINES[@]}"; do
                busid="$(awk '{ print $1 }' <<< "$line")"
                if [ "$choice" = "${busid#*-}" ]; then
                    printf '%s\n' "$line"
                    return 0
                fi
            done
        fi

        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#USBIPD_LINES[@]}" ]; then
            printf '%s\n' "${USBIPD_LINES[$((choice - 1))]}"
            return 0
        fi

        echo "Введите номер пункта, USB-номер или busid, например 1, 2 или 1-2" >&2
    done
}

wait_for_ttydev() {
    local timeout_sec="${1:-10}"
    local elapsed=0

    while [ "$elapsed" -lt "$timeout_sec" ]; do
        if ls /dev/ttyUSB* >/dev/null 2>&1 || ls /dev/ttyACM* >/dev/null 2>&1; then
            return 0
        fi

        sleep 1
        elapsed=$((elapsed + 1))
    done

    return 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel 2>/dev/null || { cd "$SCRIPT_DIR/../.." && pwd; })"

PROJECT_DIR="$(find_project_dir "${1:-.}")"

# if [ -z "$PROJECT_DIR" ]; then
#     echo "Не найден platformio.ini для: ${1:-.}" >&2
#     exit 1
# fi

VENV_DIR="${ESP32_LEVEL_VENV:-$REPO_ROOT/.venv}"
PYTHON="$VENV_DIR/bin/python"
PYTHONWARNINGS="${PYTHONWARNINGS:-ignore:::requests}"
export PYTHONWARNINGS

# if [ ! -x "$PYTHON" ]; then
#     echo "Не найден PlatformIO в $VENV_DIR" >&2
#     echo "Создайте окружение:" >&2
#     echo "  python3 -m venv .venv" >&2
#     echo "  .venv/bin/python -m pip install -r requirements.txt" >&2
#     exit 1
# fi

if ! "$PYTHON" -m platformio --version >/dev/null 2>&1; then
    echo "PlatformIO не установлен в $VENV_DIR" >&2
    echo "Установите зависимости:" >&2
    echo "  $PYTHON -m pip install -r requirements.txt" >&2
    exit 1
fi

TTY_DEVICE_GLOBS=(/dev/ttyUSB* /dev/ttyACM*)

# If any tty device already present, skip usbipd entirely
TTY_PREEXIST=false
if ls /dev/ttyUSB* >/dev/null 2>&1 || ls /dev/ttyACM* >/dev/null 2>&1; then
    TTY_PREEXIST=true
fi

if grep -qiE '(microsoft|wsl)' /proc/version 2>/dev/null && [ "$TTY_PREEXIST" = false ]; then
    USBIPD="${USBIPD_EXE:-usbipd.exe}"

    if ! command -v "$USBIPD" >/dev/null 2>&1; then
        echo "Ошибка: usbipd.exe не найден." >&2
        exit 1
    fi

    mapfile -t USBIPD_LINES < <("$USBIPD" list 2>/dev/null | tr -d '\r' | awk '/CP210x|CH340|CDC|ACM/ && $1 ~ /^[0-9]+-[0-9]+$/ { print }')

    if [ "${#USBIPD_LINES[@]}" -eq 0 ]; then
        echo "Ошибка: ESP32 не подключена (CP210x/CH340/CDC/ACM не найден)." >&2
        exit 1
    fi

    SELECTED_USBIPD_LINE="$(choose_usbipd_busid)"
    SELECTED_BUSID="$(awk '{ print $1 }' <<< "$SELECTED_USBIPD_LINE")"

    if grep -q 'Attached' <<< "$SELECTED_USBIPD_LINE"; then
        echo "usbipd already attached: $SELECTED_BUSID"
        if ! wait_for_ttydev 10; then
            echo "Ошибка: /dev/ttyUSB* не появился (already attached)." >&2
            exit 1
        fi
    else
        echo "usbipd attach: $SELECTED_BUSID"
        "$USBIPD" bind --busid "$SELECTED_BUSID" 2>/dev/null || true
        "$USBIPD" attach --wsl --busid "$SELECTED_BUSID" 2>/dev/null || true

        if ! wait_for_ttydev 10; then
            echo "Ошибка: устройство не появилось в /dev/ttyUSB* после attach." >&2
            exit 1
        fi

        for dev in /dev/ttyUSB*; do
            stty -F "$dev" 115200 2>/dev/null || true
        done
        sleep 2
    fi
fi

cd "$PROJECT_DIR"

TTY_PORTS=()
for glob in "${TTY_DEVICE_GLOBS[@]}"; do
    for dev in $glob; do
        if [ -e "$dev" ]; then
            TTY_PORTS+=("$dev")
        fi
    done
done

if [ "${#TTY_PORTS[@]}" -eq 0 ]; then
    echo "Ошибка: ни одно /dev/ttyUSB* или /dev/ttyACM* не найдено." >&2
    exit 1
fi

echo "Устройств: ${#TTY_PORTS[@]} (${TTY_PORTS[*]})"
if [ "${#TTY_PORTS[@]}" -eq 1 ]; then
    FLASH_PORT="${TTY_PORTS[0]}"
    echo "Автовыбор порта: $FLASH_PORT"
else
    FLASH_PORT="$(choose_from_list "Через какой порт шить:" "${TTY_PORTS[@]}")"
fi

flash_port() {
    local port="$1"
    echo "--- Прошивка: $port ---"
    if ! "$PYTHON" -m platformio run --target upload --upload-port "$port"; then
        echo "--- Повтор через 3s: $port ---"
        sleep 3
        "$PYTHON" -m platformio run --target upload --upload-port "$port"
    fi
}

flash_port "$FLASH_PORT"

"$PYTHON" -m platformio device monitor --port "$FLASH_PORT" --baud 115200

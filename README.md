
# esp_level
Набор PlatformIO-проектов для ESP32/ESP32-S3, общая прошивка через pip-окружение и локальные даташиты в папках проектов.

## Установка окружения

Нужен Python 3.10.

```bash
sudo apt install -y git python3.10 python3.10-venv
```

```bash
cd path/to/arduino-homeproject
python3.10 setup.py
```

Скрипт создаёт `.venv`, ставит зависимости и предзагружает все espressif32-пакеты — чтобы первая прошивка не качала их на лету.

Проверка:

```bash
which python # path/to/arduino-homeproject/.venv/bin/python
python -m platformio --version
```

Если окружение лежит не в `.venv`, задайте путь:

```bash
export ESP32_LEVEL_VENV=/path/to/venv
```

## Прошивка ESP

Общий скрипт лежит в корне:

В `flash.sh` можно передать любой путь внутри PlatformIO-проекта:

- директорию проекта;
- файл `platformio.ini`;
- файл `.ino` в `src/`;
- старый путь к `.ino` в корне проекта, если файл уже переехал в `src/`.

```bash
./flash.sh src/esp_level/test/heater
./flash.sh src/esp_level/test/heater/platformio.ini
./flash.sh src/esp_level/test/heater/src/main.ino
```

Скрипт сам поднимается вверх до ближайшего `platformio.ini`, запускает upload и затем serial monitor на `115200`.

То же самое можно вызывать через shell-функцию `run`, если она настроена в `.bashrc`:

```bash
run src/esp_level/test/heater
run src/esp_level/test/heater/platformio.ini
run src/esp_level/test/heater/src/main.ino
```

Функция `run`:

```
find_up() {
    local start="$1"
    local name="$2"
    local dir

    if [ -f "$start" ]; then
        dir="$(cd "$(dirname "$start")" && pwd)"
    else
        dir="$(cd "$start" && pwd)"
    fi

    while [ "$dir" != "/" ]; do
        if [ -e "$dir/$name" ]; then
            printf '%s\n' "$dir/$name"
            return 0
        fi
        dir="$(dirname "$dir")"
    done

    return 1
}

run() {
    local file="$1"
    local ext="${file##*.}"
    local dir="$(dirname "$file")"
    local base="$(basename "$file")"
    case "$ext" in
        ino)
            flash="$(find_up "$file" flash.sh)" || {
                echo "Не найден flash.sh выше $file" >&2
                return 1
            }
            "$flash" "$file"
            ;;
        ini)
            if [ "$base" != "platformio.ini" ]; then
                echo "нет обработчика для .$ext: $file" >&2
                return 1
            fi
            flash="$(find_up "$file" flash.sh)" || {
                echo "Не найден flash.sh выше $file" >&2
                return 1
            }
            "$flash" "$file"
            ;;
        *)           echo "нет обработчика для .$ext" ;;
    esac
}
```

## USB в WSL

Для автоматического подключения USB-UART из WSL нужен `usbipd-win` на Windows, чтобы в WSL была доступна команда:

```bash
usbipd.exe --version
```

Скрипт ищет устройства `CP210x` или `CH340`. Если автоматическое подключение не сработало, подключите плату вручную через `usbipd.exe`, затем повторите `./flash.sh <project>`.

## Даташиты

В каждой PlatformIO-папке есть `datasheet/` с материалами по плате и основным модулям проекта:

- `head/datasheet`
- `heat/datasheet`
- `led_strip/datasheet`
- `peristalsis/datasheet`
- `pnevma_i2c/datasheet`
- `robohand/datasheet`
- `vortex/datasheet`
- `position_system_i2c/firmware/datasheet`

Тесты отдельных компонентов лежат в `test/`:

- `test/ESP_UNO`
- `test/button`
- `test/pwm`
- `test/relay`

Для них тоже работает `flash.sh` и `run`:

```bash
./flash.sh test/button
./flash.sh test/pwm/platformio.ini
run src/esp_level/test/relay
```

Общая подборка лежит в `datasheets/common`; повторяющиеся изображения там оформлены как симлинки на файлы из проектных `datasheet/`.

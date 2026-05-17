# arduino-homeproject

Набор PlatformIO-проектов для ESP32 и общие материалы для схем/даташитов.

## Установка окружения

В новом WSL/Linux окружении:

```bash
sudo apt update
sudo apt install -y git python3 python3-venv python3-pip
```

Клонируйте репозиторий и создайте одно pip-окружение в корне:

```bash
git clone git@github.com:walderhu/arduino-homeproject.git
cd arduino-homeproject

python3 -m venv .venv
.venv/bin/python -m pip install -U pip setuptools wheel
.venv/bin/python -m pip install -r requirements.txt
```

Проверка:

```bash
.venv/bin/platformio --version
.venv/bin/pio --version
```

`conda activate pio` и `source /home/tru60/biochemlab/biochemlab/bin/activate` больше не нужны. Общий скрипт прошивки использует `./.venv/bin/platformio` и `./.venv/bin/pio`.

Если окружение лежит не в `.venv`, можно передать путь:

```bash
export ARDUINO_HOMEPROJECT_VENV=/path/to/venv
```

## USB в WSL

Для прошивки из WSL нужен установленный Windows `usbipd-win`, чтобы из WSL была доступна команда:

```bash
usbipd.exe --version
```

Скрипт ищет платы с USB-UART `CP210x` или `CH340` через `usbipd.exe list` и подключает их к WSL, если `/dev/ttyUSB*` еще нет.

## Функция run

Добавьте функцию в shell, например в `~/.bashrc`:

```bash
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

find_platformio_project() {
    local start="$1"
    local dir

    if [ -f "$start" ]; then
        dir="$(cd "$(dirname "$start")" && pwd)"
    else
        dir="$(cd "$start" && pwd)"
    fi

    while [ "$dir" != "/" ]; do
        if [ -f "$dir/platformio.ini" ]; then
            printf '%s\n' "$dir"
            return 0
        fi
        dir="$(dirname "$dir")"
    done

    return 1
}

run() {
    local file="$1"
    local ext="${file##*.}"
    local flash
    local project_dir

    case "$ext" in
        ino)
            project_dir="$(find_platformio_project "$file")" || {
                echo "Не найден platformio.ini для $file" >&2
                return 1
            }

            flash="$(find_up "$project_dir" flash.sh)" || {
                echo "Не найден flash.sh выше $project_dir" >&2
                return 1
            }

            "$flash" "$project_dir"
            ;;
        *) echo "Нет обработчика для .$ext" ;;
    esac
}
```

После этого можно перейти в папку проекта и прошить:

```bash
cd src/radio/NRF24L01/src
run main.ino
```

Или вызвать с абсолютным путем:

```bash
run /home/tru60/arduino-homeproject/src/radio/NRF24L01/src/main.ino
```

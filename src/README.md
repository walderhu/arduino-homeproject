

объявите заранее функцию
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

чтобы прошить есп32
перейдите в нужную папку и выполните 
```bash
run main.ino
```



объявите заранее функцию 
```bash
run() {
    local file="$1"
    local ext="${file##*.}"
    local dir="$(dirname "$file")"
    local base="$(basename "$file")"

    case "$ext" in
        ino)         cd "$dir" && ./flash.sh ;;
        *)           echo "Нет обработчика для .$ext" ;;
    esac

}


```

чтобы прошить есп32
перейдите в нужную папку и выполните 
```bash
run main.ino
```
# Move MG996R to approximately 90 degrees

The signal wire is connected to ESP32 GPIO13. The ESP32 repeatedly commands
85, 90, and 95 degrees with a one-second pause after each position.

## Wiring

| MG996R wire | Connection |
| --- | --- |
| Orange/yellow (signal) | ESP32 GPIO13 |
| Red (power) | External regulated 4.8-6.6 V supply |
| Brown/black (ground) | External supply GND and ESP32 GND |

Do not power the servo from the ESP32 3.3 V pin. The supply should tolerate the
servo's high startup/stall current (about 1.4 A or more for an original MG996R).

## Commands

```sh
pio run
pio run --target upload
pio device monitor
```

The physical angle is approximate and depends on the servo horn installation.
If the mechanism can collide near its center position, disconnect the linkage
before powering it.

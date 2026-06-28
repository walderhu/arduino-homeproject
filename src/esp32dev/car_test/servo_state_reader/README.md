# Passive servo state reader

This is a separate PlatformIO project for an ESP32 Dev Module. It never creates
a servo PWM signal and therefore does not command the servo to move. Every
100 ms it prints the analog feedback reading as raw ADC counts, approximate
voltage, and uncalibrated position percentage.

## Important limitation

A normal three-wire hobby servo has power, ground, and a PWM command input. It
does not expose its actual position, so an ESP32 cannot detect manual movement
from those three wires. This project requires one of the following:

- a feedback servo with a separate analog position output;
- a separate position sensor;
- a wire brought out from the servo's internal potentiometer.

Do not connect the normal PWM command wire to the feedback input and expect it
to report position: it only carries the requested position.

## Wiring

| Feedback servo / sensor | ESP32 |
| --- | --- |
| Analog feedback | GPIO13 |
| GND | GND |

The signal connected to GPIO13 must remain in the `0..3.3 V` range. Use a
voltage divider if the feedback output can exceed 3.3 V. Power the servo from a
suitable external supply and join its ground to ESP32 ground. Do not power a
servo from the ESP32 3.3 V pin.

The servo PWM command wire can be left disconnected. If another controller is
connected to it, this firmware still does not write to that wire.

## Build, upload, and monitor

Run from this directory:

```sh
pio run
pio run --target upload
pio device monitor
```

Serial speed is `115200 baud`. Example output:

```text
time_ms,raw,voltage_v,position_percent
1200,2034,1.639,49.7
1300,2102,1.694,51.3
```

`position_percent` is based on the entire ADC range and is not an angle. To
convert it to degrees accurately, first record the raw values at the two safe
mechanical endpoints and calibrate against those values.

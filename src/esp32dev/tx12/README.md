# TX12 External Module Bay Reader

ESP32 firmware for reading RadioMaster TX12 stick, switch, and knob state from
the external module bay.

## Important electrical notes

- ESP32 GPIO pins are 3.3V only.
- Do not connect bay `+6V` or `VBAT/VIN` to ESP32 GPIO.
- Use a level shifter or resistor divider for bay signal pins if the bay outputs
  more than 3.3V.
- Connect TX12 bay GND and ESP32 GND together.
- Power ESP32 from USB or from a proper regulator, not directly from VBAT.

## Wiring

| TX12 external bay pin | Meaning from datasheet | ESP32 |
| --- | --- | --- |
| 1 | PPM / RC signal | GPIO34 through level shifting |
| 2 | +6V | Do not connect to GPIO |
| 3 | VBAT 2S / VIN | Do not connect to GPIO |
| 4 | GND | GND |
| 5 | Signal / data / telemetry | GPIO16 RX through level shifting |

## Radio setup

In EdgeTX/OpenTX model setup:

- External RF = `CRSF`.

Open the serial monitor at `115200`. The default output is:

```text
LJ(X:0.50|Y:0.50) RJ(X:0.50|Y:0.50) A:0 B:1 C:0 D:1 E:0 F:2 S1:0.42 S2:0.77
```

Values:

- `LJ` and `RJ` are left/right stick `X/Y`, normalized to `0.00..1.00`.
- `A` and `D` are two-state buttons, printed as `0` or `1`.
- `B`, `C`, `E`, and `F` are three-state switches, printed as `0`, `1`, or `2`.
- `S1` and `S2` are knobs, normalized to `0.00..1.00`.

Default channel map:

| CRSF channel | TX12 control |
| --- | --- |
| CH1 | RJ X |
| CH2 | RJ Y |
| CH3 | LJ Y |
| CH4 | LJ X |
| CH5 | A |
| CH6 | B |
| CH7 | C |
| CH8 | D |
| CH9 | E |
| CH10 | F |
| CH11 | S1 |
| CH12 | S2 |

The firmware uses inverted CRSF UART by default because this TX12 module bay
signal is inverted relative to ESP32 RX.

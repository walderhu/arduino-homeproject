# RadioMaster Pocket External Module Bay Reader

ESP32 firmware for reading RadioMaster Pocket stick, switch, and S1 pot state from
the external module bay.

## Important electrical notes

- ESP32 GPIO pins are 3.3V only.
- Do not connect bay power/VBAT to ESP32 GPIO.
- Use a level shifter or resistor divider for bay signal pins if the bay outputs
  more than 3.3V.
- Connect Pocket nano bay GND and ESP32 GND together.
- Power ESP32 from USB or from a proper regulator, not directly from VBAT.

## Wiring

| Pocket nano bay signal | Meaning | ESP32 |
| --- | --- | --- |
| PPM / RC signal | Optional PPM input | GPIO34 through level shifting |
| Power/VBAT | Module power | Do not connect to GPIO |
| GND | Ground | GND |
| CRSF/data/telemetry | Module serial data | GPIO16 RX through level shifting |

## Radio setup

In EdgeTX/OpenTX model setup:

- External RF = `CRSF`.

Open the serial monitor at `115200`. The default output is:

```text
LJ(X:+0.00|Y:+0.00) RJ(X:+0.00|Y:+0.00) SA:0 SB:1 SC:2 SD:1 SE:0 S1:0.42
```

Values:

- `LJ` and `RJ` are left/right stick `X/Y`, normalized to `-1.00..+1.00`.
- `SA`, `SD`, and `SE` are two-state controls, printed as `0` or `1`.
- `SB` and `SC` are three-state switches, printed as `0`, `1`, or `2`.
- `S1` is the Pocket pot, normalized to `0.00..1.00`.

Default channel map:

| CRSF channel | Pocket control |
| --- | --- |
| CH1 | RJ X |
| CH2 | RJ Y |
| CH3 | LJ Y |
| CH4 | LJ X |
| CH5 | SA |
| CH6 | SB |
| CH7 | SC |
| CH8 | SD |
| CH9 | SE |
| CH10 | S1 |

The firmware uses inverted CRSF UART by default because this Pocket nano module bay
signal is inverted relative to ESP32 RX.

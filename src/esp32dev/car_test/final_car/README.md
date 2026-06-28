# Final ESP32 car controller

This standalone PlatformIO project reads CRSF from an ELRS receiver and controls
one brushed DC motor and one MG996R servo.

## Pinout

| Function | ESP32 pin | Connection |
| --- | --- | --- |
| Motor PWM | GPIO13 | Transistor/MOSFET driver input or base resistor |
| Servo signal | GPIO15 | MG996R orange/yellow signal wire |
| CRSF receive | GPIO16 | ELRS receiver TX |
| CRSF transmit | GPIO17 | ELRS receiver RX (reserved; no telemetry yet) |

All devices and power supplies must share ground. Do not power the motor or
servo from an ESP32 GPIO or its 3.3 V pin.

For a brushed motor, use a suitable transistor/MOSFET power stage and a flyback
diode. GPIO13 produces active-high 20 kHz PWM. A hardware pull-down on the
transistor input/base is recommended so the motor remains off during ESP32 boot.

Power the MG996R from a suitable external 4.8-6.6 V supply. Its signal is 50 Hz.

## Control mapping

The default RadioMaster Pocket channel assignment is:

| CRSF channel | Control | Result |
| --- | --- | --- |
| CH1 | Right stick X (`RX`) | `-1` = 95 degrees, `0` = 90, `+1` = 85 |
| CH2 | Right stick Y (`RY`) | `0..+1` = motor 0..100% |

Negative `RY` values command motor stop; this circuit does not implement motor
reverse. A small dead zone of 0.02 prevents creep near stick center.

CRSF is configured for non-inverted 420000 baud. If the transmitter controls do
not match CH1/CH2, change `STEERING_CHANNEL` and `THROTTLE_CHANNEL` in
`src/main.ino` (the indexes are zero-based).

If no valid CRSF channel frame arrives for 500 ms, failsafe turns the motor off
and centers the servo at 90 degrees.

## Build and upload

```sh
pio run
pio run --target upload
pio device monitor
```

The serial monitor runs at 115200 baud and prints signal, stick, servo, motor,
frame, and CRC status every 100 ms.

# Maximum motor PWM on GPIO13

This separate ESP32 project configures GPIO13 for 20 kHz PWM at 100% duty.
At 100% duty the output remains at logic HIGH, which commands maximum power
through an active-high transistor or MOSFET driver.

## Required power stage

- Do not connect the motor directly to GPIO13.
- Use a 3.3 V logic-level N-channel MOSFET or a suitable motor driver.
- Add a gate resistor and a gate-to-ground pull-down resistor.
- Install a flyback diode across a brushed DC motor.
- Connect ESP32 ground and motor supply ground together.
- Power the motor from a separate supply sized for its startup/stall current.

If the transistor module is active-low, 100% PWM will instead turn the motor
off; invert the duty in that case.

```sh
pio run
pio run --target upload
pio device monitor
```

# MG996R maximum command on GPIO13

This separate project continuously outputs a 50 Hz servo signal with a 2500 us
high pulse on GPIO13. This is the configured maximum servo command (12.5% duty),
not generic PWM at 100% duty.

Disconnect the mechanical linkage for the first test. A clone or a servo with a
different pulse range may reach its mechanical stop before 2500 us; prolonged
stalling can damage the servo or its power supply.

Power the MG996R from a suitable external 4.8-6.6 V supply and connect the
supply ground to ESP32 ground. Do not power it from the ESP32 3.3 V pin.

```sh
pio run
pio run --target upload
pio device monitor
```

#pragma once

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

class ThermalSensor {
    const int pin;
    OneWire oneWire;
    DallasTemperature sensors;

  public:
    ThermalSensor(int pin) : pin(pin), oneWire(pin), sensors(&oneWire) {
        pinMode(pin, INPUT_PULLUP);
        sensors.begin();
    }

    float read() {
        if (sensors.getDeviceCount() == 0) {
            sensors.begin();
        }
        if (sensors.getDeviceCount() == 0) {
            return NAN;
        }

        sensors.requestTemperatures();
        float tempC = sensors.getTempCByIndex(0);
        if (tempC == DEVICE_DISCONNECTED_C) {
            return NAN;
        }
        return tempC;
    }
};

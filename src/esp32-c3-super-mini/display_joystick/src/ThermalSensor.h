// ThermalSensor.h

/* * PROJECT: Thermal Measurement System
 * DESCRIPTION: Wrapper for DallasTemperature library to manage DS18B20
 * digital thermal sensors via OneWire protocol.
 * DATE: 2026
 */

#pragma once
#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#include <cmath>

/**
 * ThermalSensor
 * -------------
 * A class to interface with DS18B20 temperature sensors. Handles
 * initialуization, device counting, and temperature retrieval.
 */
class ThermalSensor {
  const int pin;
  OneWire oneWire;
  DallasTemperature sensors;

 public:
  /**
   * ThermalSensor (Constructor)
   * --------------------------
   * Initializes the OneWire bus and the DallasTemperature sensor suite.
   * * Parameters
   * ----------
   * pin : int, optional
   * The GPIO pin assigned to the OneWire data bus (default is 13).
   * * Notes
   * -----
   * Sets the pin mode to INPUT_PULLUP to ensure signal integrity on the
   * data line.
   */
  ThermalSensor(int pin = 13)
      : pin(pin), oneWire(OneWire(pin)), sensors(&oneWire) {
    pinMode(pin, INPUT_PULLUP);
    sensors.begin();
  }

  /**
   * count
   * -----
   * Scans the OneWire bus and returns the number of detected devices.
   * * Returns
   * -------
   * int
   * The total count of DS18B20 sensors found on the bus.
   */
  int count() { return sensors.getDeviceCount(); }

  /**
   * read
   * ----
   * Requests and retrieves the temperature from the first available sensor.
   * * Returns
   * -------
   * float
   * The temperature in Celsius. Returns NAN (Not a Number) if no
   * sensors are found or if the sensor is disconnected.
   * * Notes
   * -----
   * - Device disconnection is identified by the value DEVICE_DISCONNECTED_C
   * (-127.0).
   * - This method triggers a blocking request for temperatures on the bus.
   */
  float read() {
    if (count() == 0) {
      sensors.begin();  // rescan bus if no devices found
    }
    if (count() == 0) {
      Serial.println("[ThermalSensor] Sensor not found");
      return NAN;
    }

    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);
    if (tempC == DEVICE_DISCONNECTED_C) {  // -127.0
      Serial.println("[ThermalSensor] Sensor not responding!");
      return NAN;
    }
    return tempC;
  }
};
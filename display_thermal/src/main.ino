#include <Arduino.h>
#include <LiquidCrystal.h>
#include <WiFi.h>
#include "ThermalSensor.h"
#include "../config.h"

// RS, E, D4, D5, D6, D7
LiquidCrystal lcd(22, 23, 19, 18, 5, 17);
ThermalSensor thermal(4);

void connectWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  WiFi.begin(SSID, PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    lcd.setCursor(attempts % 16, 1);
    lcd.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Waiting NTP...");

    struct tm t;
    int tries = 0;
    while (!getLocalTime(&t) && tries < 20) {
      delay(500);
      lcd.setCursor(tries % 16, 1);
      lcd.print(".");
      tries++;
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(getLocalTime(&t) ? "NTP synced!" : "NTP failed!");
    delay(1000);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi failed!");
    delay(2000);
  }
}

void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.clear();
  connectWiFi();
}

void loop() {
  struct tm t;
  float temp = thermal.read();

  lcd.setCursor(0, 0);
  char row0[17];
  if (isnan(temp)) {
    snprintf(row0, sizeof(row0), "Sensor error!   ");
  } else {
    snprintf(row0, sizeof(row0), "Temp: %.1f C    ", temp);
  }
  lcd.print(row0);

  lcd.setCursor(0, 1);
  char row1[17];
  if (getLocalTime(&t)) {
    snprintf(row1, sizeof(row1), "Time: %02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    snprintf(row1, sizeof(row1), "No time sync    ");
  }
  lcd.print(row1);

  delay(1000);
}

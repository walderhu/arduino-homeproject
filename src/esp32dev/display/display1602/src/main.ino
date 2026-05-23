#include <Arduino.h>
#include <LiquidCrystal.h>

// RS, E, D4, D5, D6, D7
LiquidCrystal lcd(22, 23, 19, 18, 5, 17);

void setup() {
  lcd.begin(16, 2);
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Hello ESP32!");

  lcd.setCursor(0, 1);
  lcd.print("Lipatov Denis");
}

void loop() {
  lcd.setCursor(15, 1);
  lcd.blink();
  delay(500);
}

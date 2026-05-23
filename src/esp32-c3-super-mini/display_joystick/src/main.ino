#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= PINS =================
#define PIN_X 2
#define PIN_Y 1
#define PIN_SW 0

// ================= SETTINGS =================
#define DEBOUNCE 40
#define HOLD_TIME 500
#define CLICK_GAP 500  // 🔥 окно между кликами
#define UI_TIMEOUT 600 // 🔥 через сколько сбрасывать msg

// ================= STATE =================
bool btnState = HIGH;
bool lastReading = HIGH;

unsigned long lastDebounce = 0;
unsigned long pressTime = 0;
unsigned long lastRelease = 0;
unsigned long lastEventTime = 0;

uint8_t clickCount = 0;

bool holding = false;
bool holdEvent = false;
bool clickResolved = false;

const char *msg = "None";

// ================= SETUP =================
void setup() {
    Serial.begin(115200);
    Wire.begin(8, 9);

    lcd.init();
    lcd.backlight();
    lcd.clear();

    pinMode(PIN_X, INPUT);
    pinMode(PIN_Y, INPUT);
    pinMode(PIN_SW, INPUT_PULLUP);

    lcd.setCursor(0, 0);
    lcd.print("Joystick Ready");
    delay(800);
    lcd.clear();
}

// ================= LOOP =================
void loop() {

    // ===== JOYSTICK =====
    int x = analogRead(PIN_X);
    int y = analogRead(PIN_Y);

    float xf = (x - 2048) / 2048.0;
    float yf = (y - 2048) / 2048.0;

    if (fabs(xf) < 0.12)
        xf = 0;
    if (fabs(yf) < 0.12)
        yf = 0;

    // ===== BUTTON =====
    bool reading = digitalRead(PIN_SW);

    if (reading != lastReading) {
        lastDebounce = millis();
    }

    if (millis() - lastDebounce > DEBOUNCE) {

        if (reading != btnState) {
            btnState = reading;
            lastEventTime = millis(); // 🔥 любое событие

            // ===== PRESS =====
            if (btnState == LOW) {
                pressTime = millis();
                holding = false;
                holdEvent = false;
                clickResolved = false;
            }

            // ===== RELEASE =====
            else {
                unsigned long duration = millis() - pressTime;

                if (holdEvent) {
                    clickCount = 0;
                } else {
                    if (duration < HOLD_TIME) {
                        clickCount++;
                        lastRelease = millis();
                    } else {
                        clickCount = 0;
                    }
                }
            }
        }
    }

    // ===== HOLD =====
    if (btnState == LOW && !holding && millis() - pressTime > HOLD_TIME) {

        holding = true;
        holdEvent = true;
        clickCount = 0;
        msg = "HOLD";
        lastEventTime = millis();
    }

    // ===== CLICK FINISH =====
    if (!holding && clickCount > 0 && !clickResolved) {

        if (millis() - lastRelease > CLICK_GAP) {

            if (clickCount == 1)
                msg = "Single";
            else if (clickCount == 2)
                msg = "Double";
            else if (clickCount == 3)
                msg = "Triple";
            else
                msg = "Multi";

            clickCount = 0;
            clickResolved = true;
            lastEventTime = millis();
        }
    }

    // ===== AUTO RESET MSG =====
    if (millis() - lastEventTime > UI_TIMEOUT) {
        msg = "None";
        clickCount = 0;
        holding = false;
        holdEvent = false;
    }

    lastReading = reading;

    // ===== LCD =====
    lcd.setCursor(0, 0);
    char buf[17];
    snprintf(buf, sizeof(buf), "X:%+5.2f Y:%+5.2f", xf - 0.82, yf - 0.72);
    lcd.print(buf);

    lcd.setCursor(0, 1);
    snprintf(buf, sizeof(buf), "Button:%-8s", msg);
    lcd.print(buf);

    // ===== DEBUG =====
    static unsigned long t = 0;
    if (millis() - t > 150) {
        Serial.printf("State:%d Clicks:%d Hold:%d Msg:%s\n", btnState, clickCount, holding, msg);
        t = millis();
    }
}

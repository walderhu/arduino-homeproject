#include "Main.h"
#include "StepperController.h"
#include <Arduino.h>

// Pins from schema
#define STEP_PIN 14
#define DIR_PIN 13
#define EN_PIN 12

// No limit switch in test schema — pass -1
StepperController motor(STEP_PIN, DIR_PIN, EN_PIN, /*signal=*/-1,
                        /*limit=*/30.0f, /*rpm=*/60, /*microstep=*/16);

void setup() {
    Serial.begin(115200);
    Serial.println("=== Stepper test START ===");

    // Skip homing — no limit switch wired
    motor.current_state = 0.0f;

    motor.enable(true);
    Serial.println("Motor enabled");
}

void loop() {
    Serial.println(">> Move +5 cm");
    State s = motor.move_linear_cm(5.0f, 0, 10, /*safety_move=*/false);
    Serial.printf("   result: %s  pos=%.2f cm\n", s == State::OK ? "OK" : "ERR",
                  motor.current_state);
    delay(1000);

    Serial.println(">> Move -5 cm");
    s = motor.move_linear_cm(-5.0f, 0, 10, /*safety_move=*/false);
    Serial.printf("   result: %s  pos=%.2f cm\n", s == State::OK ? "OK" : "ERR",
                  motor.current_state);
    delay(1000);

    Serial.println("-- cycle done --");
    delay(2000);
}

#pragma once

#include <Arduino.h>

void servoBegin();
void servoSetFromStick(float stick11);
float servoGetAngleDeg();

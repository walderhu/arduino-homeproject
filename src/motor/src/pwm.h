#pragma once

#include <Arduino.h>

void pwmBegin();
void pwmSetDuty(float duty01);
float pwmGetDuty();

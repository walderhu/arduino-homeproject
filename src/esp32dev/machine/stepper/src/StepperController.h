/* * PROJECT: Universal Stepper Motor Controller
 * DESCRIPTION: Advanced wrapper for AccelStepper supporting both belt-driven
 * linear actuators and rotational drives with limit switch integration.
 * DATE: 2026
 */

#pragma once
#include <AccelStepper.h>

#include "Main.h"

/**
 * Global Constants
 * ----------------
 * NO_ACCEL : float
 * A very high value to effectively bypass acceleration ramps.
 */
constexpr float NO_ACCEL = 100000.0f;

/**
 * StepperController
 * -----------------
 * A comprehensive class for managing stepper motors. Supports automatic
 * calibration (homing) via limit switches, unit conversion (steps to cm/deg),
 * and dynamic acceleration calculations.
 */
class StepperController {
 public:
  /**
   * StepperController (Constructor)
   * ------------------------------
   * Configures pins, mechanical parameters, and calculates internal step
   * factors.
   * * Parameters
   * ----------
   * step_pin, dir_pin, en_pin : int
   * GPIO pins for the motor driver.
   * signal_pin : int
   * GPIO pin for the limit switch (Endstop).
   * limit : float, optional
   * Maximum movement range in cm or degrees.
   * rpm : float, optional
   * Desired rotation speed.
   * microstep : int, optional
   * Microstepping setting on the driver (e.g., 16).
   * stepsPerRevolution : int, optional
   * Native steps of the motor (default 200).
   * gear_ratio : float, optional
   * Mechanical gear reduction ratio (default 1.0).
   * invert_dir : bool, optional
   * Flips the logical direction of rotation.
   * invert_enable : bool, optional
   * Inverts the enable pin logic.
   * name : const char*, optional
   * Label for the instance.
   * deg_per_cm : float, optional
   * Conversion factor for non-belt linear drives.
   * is_belt_drive : bool, optional
   * Defines calculation logic (belt vs rotational).
   */
  StepperController(int step_pin = -1, int dir_pin = -1, int en_pin = -1,
                    int signal_pin = -1, float limit = 0, float rpm = 120,
                    int microstep = 16, int stepsPerRevolution = 200,
                    float gear_ratio = 1, bool invert_dir = false,
                    bool invert_enable = false, const char *name = "Stepper",
                    float deg_per_cm = -1, bool is_belt_drive = true)
      : step_pin(step_pin),
        dir_pin(dir_pin),
        en_pin(en_pin),
        signal_pin(signal_pin),
        name(name),
        rpm(rpm),
        microstep(microstep),
        stepsPerRevolution(stepsPerRevolution),
        gear_ratio(gear_ratio),
        limit(limit),
        invert_dir(invert_dir),
        invert_enable(invert_enable),
        current_state(NAN),
        stepper(AccelStepper::DRIVER, step_pin, dir_pin),
        deg_per_cm(deg_per_cm),
        is_belt_drive(is_belt_drive) {
    pinMode(step_pin, OUTPUT);
    pinMode(dir_pin, OUTPUT);
    pinMode(signal_pin, INPUT_PULLUP);
    stepper.setMinPulseWidth(3);
    if (en_pin >= 0) pinMode(en_pin, OUTPUT);

    enable(false);
    if (is_belt_drive) {
      mm_per_rev = pulley_teeth * tooth_pitch;
      steps_per_mm = (stepsPerRevolution * microstep * gear_ratio) / mm_per_rev;
      steps_per_cm = steps_per_mm * 10.0f;
      mm_per_step = 1.0f / steps_per_mm;
      cm_per_rev = mm_per_rev / 10.0f;
      steps_per_degree = 0;
      stepsPerSec = (rpm / 60.0f) * static_cast<float>(stepsPerRevolution) *
                    static_cast<float>(microstep) * gear_ratio;
    } else {
      steps_per_degree = (stepsPerRevolution * microstep * gear_ratio) / 360.0;
      stepsPerSec = (rpm / 60.0f) * static_cast<float>(stepsPerRevolution) *
                    static_cast<float>(microstep) * gear_ratio;
    }
  }

  /**
   * enable
   * ------
   * Controls the power state of the stepper driver.
   * * Parameters
   * ----------
   * state : bool
   * True to enable (LOW signal), False to disable (HIGH).
   */
  void enable(bool state) {
    if (en_pin >= 0) digitalWrite(en_pin, state ? LOW : HIGH);
  }

  /**
   * prehome
   * -------
   * Prepares the motor for a homing sequence by setting search speed.
   */
  void prehome(float speed = 0, bool direction = false) {
    if (speed == 0) speed = stepsPerSec;
    float dir_speed = (direction ? speed : -speed);
    if (invert_dir) dir_speed = -dir_speed;
    stepper.setMaxSpeed(speed);
    stepper.setSpeed(dir_speed);
  }

  /**
   * posthome
   * --------
   * Resets the internal coordinate system to zero after successful homing.
   */
  void posthome() {
    stepper.setSpeed(0);
    stepper.setCurrentPosition(0);
    current_state = 0;
  }

  /**
   * home
   * ----
   * Executes a full homing sequence until the limit switch is triggered.
   * * Parameters
   * ----------
   * stepsPerSec : float, optional
   * Speed of the homing movement.
   * direction : bool, optional
   * Search direction (True for positive, False for negative).
   * debounce_ms : int, optional
   * Delay after switch trigger to allow mechanical settling.
   */
  void home(float stepsPerSec = 0, bool direction = false,
            int debounce_ms = 50) {
    if (stepsPerSec == 0)
      stepsPerSec = (rpm / 60.0f) *
                    static_cast<float>(stepsPerRevolution * microstep) *
                    gear_ratio;

    prehome(stepsPerSec, direction);
    while (!isPressed()) stepper.runSpeed();
    stepper.stop();
    delay(debounce_ms);
    posthome();
  }

  /**
   * isPressed
   * ---------
   * Reads the limit switch state.
   * * Returns
   * -------
   * bool
   * True if the switch is NOT pressed (assuming NC/PULLUP logic).
   */
  bool isPressed() { return digitalRead(signal_pin) != LOW; }

  /**
   * move_linear_cm
   * --------------
   * Moves the motor to a specific linear distance using cm units.
   * * Parameters
   * ----------
   * dist_cm : float
   * Distance to travel in centimeters.
   * speed : float, optional
   * Maximum speed for this movement.
   * accelPercent : float, optional
   * Percentage of total steps to spend accelerating/decelerating.
   * safety_move : bool, optional
   * If true, prevents movement outside the 0 to 'limit' range.
   * change_state : bool, optional
   * If true, updates 'current_state' after movement completes.
   * * Returns
   * -------
   * State
   * Status of the operation (OK, ERROR, or LIMIT_ERROR).
   */
  State move_linear_cm(float dist_cm, float speed = 0, float accelPercent = 5,
                       bool safety_move = true, bool change_state = true) {
    if (!is_belt_drive) return State::ERROR;
    if (isnan(current_state)) home();
    float target = current_state + dist_cm;
    if (safety_move && (target < 0 || target > limit))
      return State::LIMIT_ERROR;

    if (speed == 0) speed = stepsPerSec;
    stepper.setMaxSpeed(speed);

    long steps = static_cast<long>(dist_cm * steps_per_cm);
    if (invert_dir) steps = -steps;

    if (accelPercent <= 0) accelPercent = 1e-4f;
    long abs_steps = llabs(steps);
    long accel_steps = static_cast<long>(abs_steps * accelPercent / 100.0f);
    if (accel_steps < 1) accel_steps = 1;
    float accel = speed * speed / (2.0f * accel_steps);

    stepper.setAcceleration(accel);
    stepper.move(steps);

    if (change_state) {
      while (stepper.distanceToGo() != 0) stepper.run();
      current_state += dist_cm;
    }
    return State::OK;
  }

  float current_state;
  AccelStepper stepper;
  float limit;
  float steps_per_cm;

 private:
  const int step_pin;
  const int dir_pin;
  const int en_pin;
  const int signal_pin;
  const char *name;

  const float rpm;
  const int microstep;
  const int stepsPerRevolution;
  float gear_ratio;
  bool invert_dir;
  bool invert_enable;
  float deg_per_cm;

  float stepsPerSec;
  float steps_per_degree;

  float mm_per_rev;
  float steps_per_mm;
  float mm_per_step;
  float cm_per_rev;
  bool is_belt_drive = true;
  int pulley_teeth = 20;
  float tooth_pitch = 2.0;  // mm for GT2 belts
};
//#############################################################################
//
// FILE:   motor.h
//
// TITLE:  6-axis H-bridge motor control via EPWM
//
// DESCRIPTION:
//   Each axis uses one ePWM A output, one DIR GPIO, and one STBY GPIO.
//   All six MC33926 channels share one EN GPIO.
//
//   Control input range: [-1.0, +1.0]
//     Positive: DIR=1
//     Negative: DIR=0
//     Zero:     PWM=0
//
//   PWM frequency: 20 kHz with the powered board's SLEW input high.
//
//#############################################################################

#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Initialize all 6 H-bridge PWM modules and direction GPIO pins.
// Call after Device_init() / Device_initGPIO() and before enabling interrupts.
//
void Motor_init(void);

//
// Set the control output for a single axis.
// axis: 0-5
// u:    [-1.0, +1.0] (clamped internally)
//
void Motor_setOutput(uint16_t axis, float u);

//
// Set all 6 axis outputs at once (u[0..5]).
//
void Motor_setAllOutputs(const float u[6]);

//
// Arm only the axes selected by bits 0-5. PWM is forced to zero before the
// selected STBY pins and shared EN are asserted.
//
void Motor_arm(uint16_t axis_mask);

//
// Safe stop: zero all axes, drive every STBY low, and drive shared EN low.
//
void Motor_stop(void);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_H

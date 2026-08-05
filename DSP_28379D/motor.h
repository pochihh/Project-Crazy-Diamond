//#############################################################################
//
// FILE:   motor.h
//
// TITLE:  6-axis H-bridge motor control via EPWM
//
// DESCRIPTION:
//   Each axis uses:
//     - One EPWM module (EPWM1-6), output A for PWM duty cycle
//     - Two GPIO direction pins for H-bridge direction control
//
//   Control input range: [-1.0, +1.0]
//     Positive: DIR1=1, DIR2=0  (forward)
//     Negative: DIR1=0, DIR2=1  (reverse)
//     Zero:     DIR1=0, DIR2=0  (brake/off)
//
//   GPIO pin assignments below MUST match your PCB.
//   Verify before connecting hardware.
//
//   PWM frequency: ~40 kHz (TBPRD=2500, TBCLK=100 MHz, UP mode)
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
// Emergency stop: zero all axes immediately.
//
void Motor_stop(void);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_H

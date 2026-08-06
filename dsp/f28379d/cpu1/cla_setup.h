//#############################################################################
//
// FILE:   cla_setup.h
//
// TITLE:  CLA initialization and configuration API
//
// DESCRIPTION:
//   Call CLA_setup() once from main() after Device_init() and before EINT.
//   It configures:
//     - RAMLS4 as CLA program RAM (CPU+CLA1 dual-access)
//     - RAMLS5 as CLA data RAM   (CPU+CLA1 dual-access)
//     - CLA message RAM init (MemCfg)
//     - CPU Timer 0 as CLA Task 1 trigger at the specified rate
//     - CLA task enables and interrupt routing
//     - In FLASH build: copies CLA program from FLASHC to RAMLS4
//
//#############################################################################

#ifndef CLA_SETUP_H
#define CLA_SETUP_H

#include <stdint.h>

//
// CLA control loop rate (Hz). Timer 0 period = SYSCLK / rate.
// Locked first-pass control rate: 5 kHz.
//
#ifndef CLA_CONTROL_RATE_HZ
#define CLA_CONTROL_RATE_HZ  5000U
#endif

//
// Initialize CLA subsystem and start CPU Timer 0.
// Call once from main(), after Device_init() / Device_initGPIO() / Interrupt_init*().
//
void CLA_setup(uint32_t rate_hz);

//
// Register the CPU-side ISR that fires after each CLA Task 1 completes.
// The ISR should read gClaToCpu and drive motors/actuators.
// If NULL is passed, no EOT interrupt is used (polling mode).
//
typedef void (*CLA_task1DoneCallback)(void);
void CLA_setTask1DoneCallback(CLA_task1DoneCallback fn);

#endif // CLA_SETUP_H

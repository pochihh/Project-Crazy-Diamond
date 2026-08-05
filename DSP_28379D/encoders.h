//#############################################################################
//
// FILE:   encoders.h
//
// TITLE:  6-axis encoder interface
//
// DESCRIPTION:
//   Provides a unified 6-axis encoder API for the F28379D.
//
//   Axis mapping:
//     Axis 0: EQEP1 (GPIO20/21, high-speed hardware quadrature decoder)
//     Axis 1: EQEP2 (GPIO54/55, high-speed hardware quadrature decoder)
//     Axis 2: EQEP3 (GPIO28/29, hardware quadrature decoder)
//     Axis 3: CPU2 GPIO quadrature (via IPC shared RAM)
//     Axis 4: CPU2 GPIO quadrature (via IPC shared RAM)
//     Axis 5: CPU2 GPIO quadrature (via IPC shared RAM)
//
//   Axes 3-5 require a companion CPU2 project that reads GPIO quadrature
//   signals via interrupt and writes counts to CPU2TOCPU1RAM. If CPU2 is
//   not running, those axes report the last known values.
//
//   EQEP pins (must match hardware):
//     EQEP1: A=GPIO20, B=GPIO21, I=GPIO23 (optional)
//     EQEP2: A=GPIO54, B=GPIO55, I=GPIO57 (optional)
//     EQEP3: A=GPIO28, B=GPIO29, I=GPIO31 (optional) — verify against PCB
//
//#############################################################################

#ifndef ENCODERS_H
#define ENCODERS_H

#include "device.h"
#include "driverlib.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// ============ IPC shared RAM layout (CPU2 → CPU1) ============
//
// CPU2 writes encoder data for axes 3-5 to CPU2TOCPU1RAM.
// CPU1 reads it here. Struct must match CPU2 project definition.
// Declared as DSECT in the linker — CPU1 reads only.
//
typedef struct {
    int32_t  enc_pos[3];     // encoder positions for axes 3, 4, 5 (counts)
    uint32_t err_count[3];   // error counts for axes 3, 4, 5
    uint32_t heartbeat;      // CPU2 heartbeat (increments ~1kHz)
} Cpu2EncoderData_t;

// CPU2 IPC data is accessed internally via a fixed hardware address pointer.
// CPU2 project must write its encoder data to CPU2TOCPU1RAM starting at 0x03F800.

//
// ============ Encoder position and error state ============
//
// CPU1 encoder positions (axes 0-2), updated by Encoders_updatePosition().
// Axes 3-5 are read from gCpu2EncoderData in Encoders_updatePosition().
//
extern volatile int32_t  gEncPos[6];      // positions [counts]
extern volatile uint32_t gEncErrors[6];   // cumulative error counts

//
// ============ Qualification mode ============
//
typedef enum {
    ENCODER_QUAL_ASYNC    = 0,  // No filtering (fastest)
    ENCODER_QUAL_3SAMPLE,       // 3-sample majority vote
    ENCODER_QUAL_6SAMPLE,       // 6-sample majority vote
    ENCODER_QUAL_8SAMPLE        // 8-sample majority vote
} EncoderQualMode;

//
// ============ API ============
//

// Initialize EQEP1, EQEP2, EQEP3 and zero all positions.
// Call after Device_init() / Device_initGPIO().
void Encoders_init(void);

// Read EQEP positions into gEncPos[0..2] and copy gCpu2EncoderData into
// gEncPos[3..5]. Also logs and counts EQEP error flags.
// Call frequently from the main loop (typically every ~1 ms or faster).
void Encoders_updatePosition(void);

// Zero all encoder positions (EQEP + CPU2 IPC offset).
void Encoders_zeroPosition(void);

// Apply GPIO input qualification to EQEP1, EQEP2, or EQEP3 pins.
void Encoders_applyQualificationEQEP1(EncoderQualMode mode, uint32_t f_qual_hz,
                                       bool includeIndex);
void Encoders_applyQualificationEQEP2(EncoderQualMode mode, uint32_t f_qual_hz,
                                       bool includeIndex);
void Encoders_applyQualificationEQEP3(EncoderQualMode mode, uint32_t f_qual_hz,
                                       bool includeIndex);

#ifdef __cplusplus
}
#endif

#endif // ENCODERS_H

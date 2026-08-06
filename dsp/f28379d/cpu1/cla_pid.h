//#############################################################################
//
// FILE:   cla_pid.h
//
// TITLE:  6-axis CLA PID — shared data structures
//
// DESCRIPTION:
//   Defines the three structs shared between CPU and CLA:
//     CpuToClaMsg_t — CPU writes setpoints/positions, CLA reads
//     ClaToCpuMsg_t — CLA writes control outputs, CPU reads
//     ClaGains_t    — CPU writes PID gains, CLA reads (dual-access SARAM)
//
//   Physical placement:
//     CpuToClaMsg_t → CpuToCla1MsgRAM (hardware 0x001480, 64 words)
//     ClaToCpuMsg_t → Cla1ToCpuMsgRAM (hardware 0x001500, 64 words)
//     ClaGains_t    → Cla1DataRam0 (RAMLS5, CPU+CLA1 dual-access)
//
//   Update gains by calling CLA_setGains() from the CPU main loop.
//   The CLA uses gains on the next task invocation; no synchronization needed
//   since gains change slowly relative to the control loop rate.
//
//#############################################################################

#ifndef CLA_PID_H
#define CLA_PID_H

#include <stdint.h>

//
// Number of controlled axes
//
#define CLA_NUM_AXES  6

//
// ============ CPU -> CLA message (CPU writes, CLA reads) ============
//
// Placed in CpuToCla1MsgRAM (hardware address 0x001480, 64 words max).
// Word count: ref[6]=12 + pos[6]=12 = 24 words. Fits.
//
typedef struct {
    float    ref[CLA_NUM_AXES];   // reference setpoints (from SPI RX frame)
    int32_t  pos[CLA_NUM_AXES];   // current encoder positions (counts)
} CpuToClaMsg_t;

//
// ============ CLA -> CPU message (CLA writes, CPU reads) ============
//
// Placed in Cla1ToCpuMsgRAM (hardware address 0x001500, 64 words max).
// Word count: u[6]=12 + err_flags=1 + cycle_count=2 = 15 words. Fits.
//
typedef struct {
    float    u[CLA_NUM_AXES];     // control outputs, range [-1.0, +1.0]
    uint16_t err_flags;           // bit N set: axis N output was saturated
    uint32_t cycle_count;         // CLA heartbeat (increments each task run)
} ClaToCpuMsg_t;

//
// ============ CLA Gains (CPU writes, both CPU+CLA read) ============
//
// Placed in Cla1DataRam0 (RAMLS5, configured CPU+CLA1 dual-access).
// Word count: 4 arrays × 6 floats × 2 words = 48 words. Fits.
//
// Gains are pre-scaled on the CPU to avoid division inside the CLA task:
//   ki_dt[k]    = ki[k] * dt          (avoids multiply at task time)
//   kd_inv_dt[k] = kd[k] / dt         (avoids divide at task time)
// where dt = 1 / control_rate_hz
//
typedef struct {
    float kp[CLA_NUM_AXES];           // proportional gain
    float ki_dt[CLA_NUM_AXES];        // integral gain × dt
    float kd_inv_dt[CLA_NUM_AXES];    // derivative gain / dt
    float u_max[CLA_NUM_AXES];        // output saturation limit (> 0)
} ClaGains_t;

//
// ============ Shared variable declarations ============
//
// Definitions with DATA_SECTION pragmas are in cla_setup.c.
// The .cla file externs them directly.
//
extern volatile CpuToClaMsg_t gCpuToCla;
extern volatile ClaToCpuMsg_t gClaToCpu;
extern ClaGains_t gClaGains;

// CLA task entry point; CPU1 maps this address into MVECT1 during setup.
__interrupt void Cla1Task1(void);

//
// ============ Gain update helper ============
//
// Call from CPU main loop or SPI callback to update PID gains.
// control_rate_hz: CLA task rate in Hz (e.g. 1000 for 1 kHz).
//
void CLA_setGains(uint16_t axis, float kp, float ki, float kd, float u_max,
                  float control_rate_hz);

//
// Reset integrator state for one or all axes.
// axis = 0xFF resets all axes.
//
void CLA_resetIntegrators(uint16_t axis);

#endif // CLA_PID_H

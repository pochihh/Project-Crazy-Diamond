//#############################################################################
//
// FILE:   main.c
//
// TITLE:  Integration main — DMA SPI + CLA PID + Encoders + Motors
//
// DESCRIPTION:
//   Full robot control loop:
//     - SPI DMA slave: receives RPi commands (ref[6], cmd)
//     - CLA PID:       runs at 5 kHz, computes u[6] from ref/pos
//     - Encoders:      EQEP1/2/3 (CPU1) + GPIO quadrature via CPU2 IPC
//     - ADC:           reserved channels remain disabled in this PCB profile
//     - Motors:        6-axis H-bridge driven from CLA output
//
//   Control loop flow:
//     1. main loop polls encoder state
//     2. CPU writes gEncPos[] → gCpuToCla.pos[] before each CLA tick
//     3. CLA Task 1 fires at 5 kHz and snapshots control telemetry
//     4. CLA1_Task_1_ISR (PIE 11.1): applies the latest host duty command
//        and calls SpiDma_updateTx() with latest telemetry
//     5. SPI ARM/DUTY/DISARM commands update the staged host command
//
//   Safety:
//     - If SPI duty command is not received within CMD_TIMEOUT_US, shared EN
//       and all STBY/PWM outputs are disabled at the next 5 kHz boundary.
//
// BUILD:
//   CCS RAM and FLASH configurations select the matching linker command file.
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "spi_dma.h"
#include "cla_pid.h"
#include "cla_setup.h"
#include "encoders.h"
#include "motor.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

//
// ============ Microsecond timer (CPU Timer 1) ============
//
static void microsTimer_init(void)
{
    CPUTimer_stopTimer(CPUTIMER1_BASE);
    CPUTimer_setPreScaler(CPUTIMER1_BASE,
                          (uint16_t)(DEVICE_SYSCLK_FREQ / 1000000U) - 1U);
    CPUTimer_setPeriod(CPUTIMER1_BASE, 0xFFFFFFFFU);
    CPUTimer_reloadTimerCounter(CPUTIMER1_BASE);
    CPUTimer_startTimer(CPUTIMER1_BASE);
}

static uint32_t micros(void)
{
    // Timer counts down directly at 1 MHz and wraps naturally at 2^32 us.
    return 0xFFFFFFFFU - CPUTimer_getTimerCount(CPUTIMER1_BASE);
}

//
// ============ Reserved ADC telemetry ============
//
static uint16_t gAdc[6] = {0};

//
// ============ SPI command handling ============
//
#define CMD_TIMEOUT_US      50000U   // 50 ms: zero refs if no command

static volatile uint32_t gLastCmdTimeUs = 0U;
static volatile uint16_t gRequestedArmMask = 0U;
static volatile uint16_t gArmedMask = 0U;
static volatile float gHostDuty[6] = {0.0f};

// Called from SPI DMA CS ISR when a valid RX frame arrives
static void onSpiFrame(const RxFrame_t *rx)
{
    uint16_t k;
    float duty[6];

    if (rx->cmd == RX_CMD_DISARM) {
        gRequestedArmMask = 0U;
        for (k = 0U; k < 6U; k++) gHostDuty[k] = 0.0f;
        gLastCmdTimeUs = micros();
        return;
    }

    if ((rx->cmd & 0xFFFFFF00UL) == RX_CMD_ARM_PREFIX) {
        uint16_t mask = (uint16_t)(rx->cmd & 0x00FFU);
        if ((mask & 0xFFC0U) == 0U) {
            for (k = 0U; k < 6U; k++) gHostDuty[k] = 0.0f;
            gRequestedArmMask = mask;
            gLastCmdTimeUs = micros();
        }
        return;
    }

    if (rx->cmd != RX_CMD_DUTY || gRequestedArmMask == 0U) return;

    for (k = 0U; k < 6U; k++) {
        duty[k] = rx->ref[k];
        // This comparison also rejects NaN and infinities.
        if (!(duty[k] >= -1.0f && duty[k] <= 1.0f)) return;
    }
    for (k = 0U; k < 6U; k++) {
        gHostDuty[k] = duty[k];
    }
    gLastCmdTimeUs = micros();
}

//
// ============ CLA Task 1 done callback ============
//
// Runs from PIE 11.1 ISR after each 5 kHz CLA tick.
// Reads gClaToCpu.u[] and drives motors.
// Also prepares and queues the next SPI TX frame.
//
static void onClaTask1Done(void)
{
    uint16_t k;
    float u[6];
    uint16_t requested_mask = gRequestedArmMask;

    if (requested_mask != 0U &&
        (micros() - gLastCmdTimeUs) > CMD_TIMEOUT_US) {
        requested_mask = 0U;
        gRequestedArmMask = 0U;
        for (k = 0U; k < 6U; k++) gHostDuty[k] = 0.0f;
    }

    if (requested_mask != gArmedMask) {
        Motor_stop();
        if (requested_mask != 0U) Motor_arm(requested_mask);
        gArmedMask = requested_mask;
    }

    for (k = 0U; k < 6U; k++) {
        u[k] = (gArmedMask & (uint16_t)(1U << k)) ? gHostDuty[k] : 0.0f;
    }

    if (gArmedMask != 0U) Motor_setAllOutputs(u);

    // Queue next SPI TX frame with latest telemetry
    TxFrame_t tx;
    tx.timestamp_us = micros();
    tx.err_bitmap   = gClaToCpu.err_flags;
    tx.err_count    = (uint16_t)(gClaToCpu.cycle_count & 0xFFFFU);

    for (k = 0U; k < 6U; k++) {
        tx.ref[k] = gHostDuty[k];
        tx.pos[k] = gEncPos[k];
        tx.u[k]   = u[k];
        tx.adc[k] = gAdc[k];
    }

    // IMU quaternion placeholder (zeros until IMU driver added)
    tx.quat[0] = 1.0f; tx.quat[1] = 0.0f;
    tx.quat[2] = 0.0f; tx.quat[3] = 0.0f;

    SpiDma_updateTx(&tx);
}

//
// ============ main ============
//
void main(void)
{
    uint16_t k;

    // --- System init ---
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // --- Peripheral init ---
    microsTimer_init();
    Encoders_init();
    Motor_init();
    Motor_stop();

    // --- SPI DMA slave ---
    SpiDma_init();
    SpiDma_setRxCallback(&onSpiFrame);

    // --- CLA PID ---
    CLA_setTask1DoneCallback(&onClaTask1Done);
    CLA_setup(CLA_CONTROL_RATE_HZ);

    // Set default gains after CLA_setup() initializes its shared memory.
    for (k = 0U; k < 6U; k++) {
        CLA_setGains(k,
                     0.1f,   // kp
                     0.0f,   // ki
                     0.0f,   // kd
                     1.0f,   // u_max
                     (float)CLA_CONTROL_RATE_HZ);
    }

    // Prime initial state
    for (k = 0U; k < 6U; k++) {
        gCpuToCla.ref[k] = 0.0f;
        gCpuToCla.pos[k] = 0;
    }
    CLA_resetIntegrators(0xFFU);

    gLastCmdTimeUs = micros();

    EINT;
    ERTM;

    // --- Main loop ---
    // Runs as fast as possible. CLA tick is independently triggered by Timer 0.
    //
    while (true) {
        // Update encoder positions → write to CLA input message RAM
        Encoders_updatePosition();
        for (k = 0U; k < 6U; k++) {
            gCpuToCla.pos[k] = gEncPos[k];
        }

        // LED heartbeat: blink LED1 at ~2 Hz using CLA cycle count
        // (LED2 is driven by SPI DMA on each valid frame)
        static uint32_t led_last_count = 0U;
        uint32_t cyc = gClaToCpu.cycle_count;
        if ((cyc - led_last_count) >= 500U) {   // every 500 CLA ticks = 0.5 s
            led_last_count = cyc;
            GPIO_togglePin(DEVICE_GPIO_PIN_LED1);
        }
    }
}

//#############################################################################
//
// FILE:   motor.c
//
// TITLE:  6-axis H-bridge motor control via EPWM
//
// GPIO pin assignment summary (update to match PCB):
//
//   Axis  EPWM (PWM pin)   DIR1 GPIO   DIR2 GPIO
//   ----  ---------------  ----------  ----------
//     0   EPWM1A (GPIO0)   GPIO24      GPIO25
//     1   EPWM2A (GPIO2)   GPIO26      GPIO27
//     2   EPWM3A (GPIO4)   GPIO32      GPIO33
//     3   EPWM4A (GPIO6)   GPIO34      GPIO35
//     4   EPWM5A (GPIO8)   GPIO36      GPIO37
//     5   EPWM6A (GPIO10)  GPIO38      GPIO39
//
//#############################################################################

#include "motor.h"
#include "driverlib.h"
#include "device.h"

//
// ============ Pin configuration — update to match your PCB ============
//
#define NUM_AXES  6U

// EPWM base addresses for axes 0-5
static const uint32_t kEpwmBase[NUM_AXES] = {
    EPWM1_BASE, EPWM2_BASE, EPWM3_BASE,
    EPWM4_BASE, EPWM5_BASE, EPWM6_BASE
};

// PWM output GPIO pins (EPWM A output)
static const uint32_t kPwmGpio[NUM_AXES] = { 0U, 2U, 4U, 6U, 8U, 10U };

// PWM pin config mux values (from pin_map.h)
static const uint32_t kPwmPinCfg[NUM_AXES] = {
    GPIO_0_EPWM1A,  GPIO_2_EPWM2A,  GPIO_4_EPWM3A,
    GPIO_6_EPWM4A,  GPIO_8_EPWM5A,  GPIO_10_EPWM6A
};

// EPWM peripheral clock enables
static const SysCtl_PeripheralPCLOCKCR kEpwmClk[NUM_AXES] = {
    SYSCTL_PERIPH_CLK_EPWM1, SYSCTL_PERIPH_CLK_EPWM2,
    SYSCTL_PERIPH_CLK_EPWM3, SYSCTL_PERIPH_CLK_EPWM4,
    SYSCTL_PERIPH_CLK_EPWM5, SYSCTL_PERIPH_CLK_EPWM6
};

// Direction GPIO pin 1 (forward signal)
static const uint32_t kDir1Gpio[NUM_AXES] = { 24U, 26U, 32U, 34U, 36U, 38U };

// Direction GPIO pin 2 (reverse signal)
static const uint32_t kDir2Gpio[NUM_AXES] = { 25U, 27U, 33U, 35U, 37U, 39U };

//
// PWM period: TBPRD=2500, TBCLK=100MHz (CLKDIV=1, HSPCLKDIV=2), UP mode
// → fPWM = 100 MHz / 2500 = 40 kHz
//
#define PWM_TBPRD  2500U

//
// ============ Internal helpers ============
//
static void pwm_init_one(uint32_t base)
{
    EPWM_setEmulationMode(base, EPWM_EMULATION_FREE_RUN);
    EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_2);
    EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP);
    EPWM_disablePhaseShiftLoad(base);
    EPWM_setTimeBaseCounter(base, 0U);
    EPWM_setPeriodLoadMode(base, EPWM_PERIOD_SHADOW_LOAD);
    EPWM_setTimeBasePeriod(base, PWM_TBPRD);

    EPWM_setCounterCompareShadowLoadMode(base, EPWM_COUNTER_COMPARE_A,
                                         EPWM_COMP_LOAD_ON_CNTR_ZERO);
    EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A, 0U);

    // AQ: set on ZERO, clear on UP-CMPA (non-inverted PWM)
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_HIGH,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_LOW,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);

    // Disable all trip-zone signals to prevent accidental shutoff
    EPWM_disableTripZoneSignals(base,
        EPWM_TZ_SIGNAL_OSHT1 | EPWM_TZ_SIGNAL_OSHT2 | EPWM_TZ_SIGNAL_OSHT3 |
        EPWM_TZ_SIGNAL_OSHT4 | EPWM_TZ_SIGNAL_OSHT5 | EPWM_TZ_SIGNAL_OSHT6 |
        EPWM_TZ_SIGNAL_CBC1  | EPWM_TZ_SIGNAL_CBC2  | EPWM_TZ_SIGNAL_CBC3  |
        EPWM_TZ_SIGNAL_CBC4  | EPWM_TZ_SIGNAL_CBC5  | EPWM_TZ_SIGNAL_CBC6  |
        EPWM_TZ_SIGNAL_DCAEVT1 | EPWM_TZ_SIGNAL_DCAEVT2 |
        EPWM_TZ_SIGNAL_DCBEVT1 | EPWM_TZ_SIGNAL_DCBEVT2);
    EPWM_clearTripZoneFlag(base,
        EPWM_TZ_FLAG_OST | EPWM_TZ_FLAG_CBC |
        EPWM_TZ_FLAG_DCAEVT1 | EPWM_TZ_FLAG_DCAEVT2 |
        EPWM_TZ_FLAG_DCBEVT1 | EPWM_TZ_FLAG_DCBEVT2);
}

//
// ============ Motor_init ============
//
void Motor_init(void)
{
    uint16_t k;

    // Enable EPWM peripheral clocks
    for (k = 0U; k < NUM_AXES; k++) {
        SysCtl_enablePeripheral(kEpwmClk[k]);
    }

    // Configure PWM GPIO pins
    for (k = 0U; k < NUM_AXES; k++) {
        GPIO_setPinConfig(kPwmPinCfg[k]);
        GPIO_setDirectionMode(kPwmGpio[k], GPIO_DIR_MODE_OUT);
        GPIO_setQualificationMode(kPwmGpio[k], GPIO_QUAL_SYNC);
    }

    // Configure direction GPIO pins (output, start low = brake)
    for (k = 0U; k < NUM_AXES; k++) {
        GPIO_setDirectionMode(kDir1Gpio[k], GPIO_DIR_MODE_OUT);
        GPIO_writePin(kDir1Gpio[k], 0U);
        GPIO_setDirectionMode(kDir2Gpio[k], GPIO_DIR_MODE_OUT);
        GPIO_writePin(kDir2Gpio[k], 0U);
    }

    // Pause TBCLK while configuring all PWM modules
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
    for (k = 0U; k < NUM_AXES; k++) {
        pwm_init_one(kEpwmBase[k]);
    }
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
}

//
// ============ Motor_setOutput ============
//
void Motor_setOutput(uint16_t axis, float u)
{
    if (axis >= NUM_AXES) return;

    // Clamp
    if (u > 1.0f)  u = 1.0f;
    if (u < -1.0f) u = -1.0f;

    uint16_t pwm_val;

    if (u > 0.0f) {
        GPIO_writePin(kDir1Gpio[axis], 1U);
        GPIO_writePin(kDir2Gpio[axis], 0U);
        pwm_val = (uint16_t)(u * (float)PWM_TBPRD);
    } else if (u < 0.0f) {
        GPIO_writePin(kDir1Gpio[axis], 0U);
        GPIO_writePin(kDir2Gpio[axis], 1U);
        pwm_val = (uint16_t)(-u * (float)PWM_TBPRD);
    } else {
        GPIO_writePin(kDir1Gpio[axis], 0U);
        GPIO_writePin(kDir2Gpio[axis], 0U);
        pwm_val = 0U;
    }

    EPWM_setCounterCompareValue(kEpwmBase[axis], EPWM_COUNTER_COMPARE_A,
                                pwm_val);
}

//
// ============ Motor_setAllOutputs ============
//
void Motor_setAllOutputs(const float u[6])
{
    uint16_t k;
    for (k = 0U; k < NUM_AXES; k++) {
        Motor_setOutput(k, u[k]);
    }
}

//
// ============ Motor_stop ============
//
void Motor_stop(void)
{
    uint16_t k;
    for (k = 0U; k < NUM_AXES; k++) {
        Motor_setOutput(k, 0.0f);
    }
}

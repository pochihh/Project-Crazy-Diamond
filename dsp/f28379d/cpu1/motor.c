//#############################################################################
//
// FILE:   motor.c
//
// TITLE:  6-axis H-bridge motor control via EPWM
//
// GPIO pin assignment summary:
//
//   Axis  EPWM (PWM pin)   STBY GPIO   DIR GPIO
//   ----  ---------------  ----------  --------
//     0   EPWM1A (GPIO0)   GPIO1       GPIO12
//     1   EPWM2A (GPIO2)   GPIO3       GPIO13
//     2   EPWM3A (GPIO4)   GPIO5       GPIO14
//     3   EPWM4A (GPIO6)   GPIO7       GPIO15
//     4   EPWM5A (GPIO8)   GPIO9       GPIO16
//     5   EPWM6A (GPIO10)  GPIO11      GPIO17
//
//   Shared MC33926 EN: GPIO18. Boot and Motor_stop() hold EN and STBY low.
//
//#############################################################################

#include "motor.h"
#include "driverlib.h"
#include "device.h"

//
// ============ Pin configuration ============
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

static const uint32_t kStbyGpio[NUM_AXES] = { 1U, 3U, 5U, 7U, 9U, 11U };
static const uint32_t kDirGpio[NUM_AXES] = { 12U, 13U, 14U, 15U, 16U, 17U };

#define MOTOR_EN_GPIO  18U

//
// PWM period: TBPRD=5000, TBCLK=100MHz (CLKDIV=1, HSPCLKDIV=1), UP mode
// → fPWM = 100 MHz / 5000 = 20 kHz. The powered PCB drives SLEW high.
//
#define PWM_TBPRD  5000U

//
// ============ Internal helpers ============
//
static void pwm_init_one(uint32_t base)
{
    EPWM_setEmulationMode(base, EPWM_EMULATION_FREE_RUN);
    EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);
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

    // Establish the board-safe state before peripheral mux changes.
    GPIO_writePin(MOTOR_EN_GPIO, 0U);
    GPIO_setDirectionMode(MOTOR_EN_GPIO, GPIO_DIR_MODE_OUT);
    for (k = 0U; k < NUM_AXES; k++) {
        GPIO_writePin(kStbyGpio[k], 0U);
        GPIO_setDirectionMode(kStbyGpio[k], GPIO_DIR_MODE_OUT);
        GPIO_writePin(kDirGpio[k], 0U);
        GPIO_setDirectionMode(kDirGpio[k], GPIO_DIR_MODE_OUT);
    }

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
        GPIO_writePin(kDirGpio[axis], 1U);
        pwm_val = (uint16_t)(u * (float)PWM_TBPRD);
    } else if (u < 0.0f) {
        GPIO_writePin(kDirGpio[axis], 0U);
        pwm_val = (uint16_t)(-u * (float)PWM_TBPRD);
    } else {
        GPIO_writePin(kDirGpio[axis], 0U);
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
// ============ Motor_arm ============
//
void Motor_arm(uint16_t axis_mask)
{
    uint16_t k;
    axis_mask &= 0x003FU;

    GPIO_writePin(MOTOR_EN_GPIO, 0U);
    for (k = 0U; k < NUM_AXES; k++) {
        Motor_setOutput(k, 0.0f);
        GPIO_writePin(kStbyGpio[k],
                      (axis_mask & (uint16_t)(1U << k)) ? 1U : 0U);
    }
    if (axis_mask != 0U) {
        GPIO_writePin(MOTOR_EN_GPIO, 1U);
    }
}

//
// ============ Motor_stop ============
//
void Motor_stop(void)
{
    uint16_t k;
    GPIO_writePin(MOTOR_EN_GPIO, 0U);
    for (k = 0U; k < NUM_AXES; k++) {
        Motor_setOutput(k, 0.0f);
        GPIO_writePin(kStbyGpio[k], 0U);
    }
}

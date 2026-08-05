#include "motor.h"
#include "board.h"
#include "driverlib.h"

// Motor direction control GPIO pins
// Motor A: EPWM4A (GPIO6) for PWM + GPIO8/GPIO9 for direction
// Motor B: EPWM6A (GPIO10) for PWM + GPIO14/GPIO15 for direction
#define MOTOR_A_DIR1_GPIO 8U
#define MOTOR_A_DIR2_GPIO 9U
#define MOTOR_B_DIR1_GPIO 14U
#define MOTOR_B_DIR2_GPIO 15U

/*---------------------------------------------------------------------------
 * H‑bridge PWM and GPIO initialisation
 *
 * Motor A uses EPWM4A (GPIO6) for PWM duty cycle; GPIO8/GPIO9 for direction
 *control. Motor B uses EPWM6A (GPIO10) for PWM duty cycle; GPIO14/GPIO15 for
 *direction control.
 *
 * Simple H-bridge control: Single PWM + Direction pins
 *--------------------------------------------------------------------------*/
static void pwm_init_edge_up(uint32_t base, uint16_t tbprd) {
  // Free-run during debug
  EPWM_setEmulationMode(base, EPWM_EMULATION_FREE_RUN);

  // TBCLK = SYSCLK/(HSPCLKDIV*CLKDIV) = 200MHz/(2*1)=100MHz
  EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_2);

  // Edge-aligned PWM (UP count)
  EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP);
  EPWM_disablePhaseShiftLoad(base);
  EPWM_setTimeBaseCounter(base, 0);

  // Period & shadowing
  EPWM_setPeriodLoadMode(base, EPWM_PERIOD_SHADOW_LOAD);
  EPWM_setTimeBasePeriod(base, tbprd);

  // CMPA shadow, load at ZERO; start at 0% duty
  EPWM_setCounterCompareShadowLoadMode(base, EPWM_COUNTER_COMPARE_A,
                                       EPWM_COMP_LOAD_ON_CNTR_ZERO);
  EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A, 0);

  // AQ for UP mode: set on ZERO, clear on UP-CMPA
  EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_HIGH,
                                EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
  EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_LOW,
                                EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);

  //   EPWM_disableTripZoneSignals(base, 0xFFFF);
  //   EPWM_clearTripZoneFlag(base, 0xFFFF);
  // Disable all TZ signal selections (one-shot + cycle-by-cycle + digital
  // compare)
  EPWM_disableTripZoneSignals(
      base,
      EPWM_TZ_SIGNAL_OSHT1 | EPWM_TZ_SIGNAL_OSHT2 | EPWM_TZ_SIGNAL_OSHT3 |
          EPWM_TZ_SIGNAL_OSHT4 | EPWM_TZ_SIGNAL_OSHT5 | EPWM_TZ_SIGNAL_OSHT6 |
          EPWM_TZ_SIGNAL_CBC1 | EPWM_TZ_SIGNAL_CBC2 | EPWM_TZ_SIGNAL_CBC3 |
          EPWM_TZ_SIGNAL_CBC4 | EPWM_TZ_SIGNAL_CBC5 | EPWM_TZ_SIGNAL_CBC6 |
          EPWM_TZ_SIGNAL_DCAEVT1 | EPWM_TZ_SIGNAL_DCAEVT2 |
          EPWM_TZ_SIGNAL_DCBEVT1 | EPWM_TZ_SIGNAL_DCBEVT2);

  // Clear any latched TZ flags
  EPWM_clearTripZoneFlag(base, EPWM_TZ_FLAG_OST | EPWM_TZ_FLAG_CBC |
                                   EPWM_TZ_FLAG_DCAEVT1 | EPWM_TZ_FLAG_DCAEVT2 |
                                   EPWM_TZ_FLAG_DCBEVT1 | EPWM_TZ_FLAG_DCBEVT2);
}

void HBridge_init(void) {
  const uint16_t tbprd = 2500; // ~40 kHz in UP mode with TBCLK=100 MHz

  // Enable ePWM module clocks
  SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM4);
  SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM6);

  // PWM pinmux
  GPIO_setPinConfig(GPIO_6_EPWM4A);
  GPIO_setDirectionMode(6U, GPIO_DIR_MODE_OUT);
  GPIO_setQualificationMode(6U, GPIO_QUAL_SYNC);

  GPIO_setPinConfig(GPIO_10_EPWM6A);
  GPIO_setDirectionMode(10U, GPIO_DIR_MODE_OUT);
  GPIO_setQualificationMode(10U, GPIO_QUAL_SYNC);

  // Direction pins
  GPIO_setPinConfig(GPIO_8_GPIO8);
  GPIO_setDirectionMode(8U, GPIO_DIR_MODE_OUT);
  GPIO_writePin(8U, 0);
  GPIO_setPinConfig(GPIO_9_GPIO9);
  GPIO_setDirectionMode(9U, GPIO_DIR_MODE_OUT);
  GPIO_writePin(9U, 0);
  GPIO_setPinConfig(GPIO_14_GPIO14);
  GPIO_setDirectionMode(14U, GPIO_DIR_MODE_OUT);
  GPIO_writePin(14U, 0);
  GPIO_setPinConfig(GPIO_15_GPIO15);
  GPIO_setDirectionMode(15U, GPIO_DIR_MODE_OUT);
  GPIO_writePin(15U, 0);

  // Halt TB clocks while configuring to avoid glitches
  SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

  // Configure both PWMs
  pwm_init_edge_up(EPWM4_BASE, tbprd);
  pwm_init_edge_up(EPWM6_BASE, tbprd);

  // Start TB clocks
  SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

  // Ensure 0% duty at start
  EPWM_setCounterCompareValue(EPWM4_BASE, EPWM_COUNTER_COMPARE_A, 0);
  EPWM_setCounterCompareValue(EPWM6_BASE, EPWM_COUNTER_COMPARE_A, 0);
}

/*---------------------------------------------------------------------------
 * Control the H-bridge to drive motor
 * Input control (-1.0 to 1.0) to drive the H-bridge
 * Sign for direction and value for duty cycle
 *--------------------------------------------------------------------------*/
void motorControl(float controlInputA, float controlInputB) {
  uint16_t pwmValueA, pwmValueB;
  uint16_t period =
      2500; // Use 2500 as the period for both motors (same as H_Bridge_A_TBPRD)

  // Clamp control inputs to valid range
  if (controlInputA > 1.0f)
    controlInputA = 1.0f;
  if (controlInputA < -1.0f)
    controlInputA = -1.0f;
  if (controlInputB > 1.0f)
    controlInputB = 1.0f;
  if (controlInputB < -1.0f)
    controlInputB = -1.0f;

  // Motor A control
  if (controlInputA > 0.0f) {
    // Forward direction
    GPIO_writePin(MOTOR_A_DIR1_GPIO, 1);
    GPIO_writePin(MOTOR_A_DIR2_GPIO, 0);
    pwmValueA = (uint16_t)(controlInputA * period);
  } else if (controlInputA < 0.0f) {
    // Reverse direction
    GPIO_writePin(MOTOR_A_DIR1_GPIO, 0);
    GPIO_writePin(MOTOR_A_DIR2_GPIO, 1);
    pwmValueA = (uint16_t)(-controlInputA * period);
  } else {
    // Stop motor
    GPIO_writePin(MOTOR_A_DIR1_GPIO, 0);
    GPIO_writePin(MOTOR_A_DIR2_GPIO, 0);
    pwmValueA = 0;
  }

  // Motor B control
  if (controlInputB > 0.0f) {
    // Forward direction
    GPIO_writePin(MOTOR_B_DIR1_GPIO, 1);
    GPIO_writePin(MOTOR_B_DIR2_GPIO, 0);
    pwmValueB = (uint16_t)(controlInputB * period);
  } else if (controlInputB < 0.0f) {
    // Reverse direction
    GPIO_writePin(MOTOR_B_DIR1_GPIO, 0);
    GPIO_writePin(MOTOR_B_DIR2_GPIO, 1);
    pwmValueB = (uint16_t)(-controlInputB * period);
  } else {
    // Stop motor
    GPIO_writePin(MOTOR_B_DIR1_GPIO, 0);
    GPIO_writePin(MOTOR_B_DIR2_GPIO, 0);
    pwmValueB = 0;
  }

  // Update PWM compare values (single PWM per motor)
  EPWM_setCounterCompareValue(EPWM4_BASE, EPWM_COUNTER_COMPARE_A,
                              pwmValueA); // Motor A PWM
  EPWM_setCounterCompareValue(EPWM6_BASE, EPWM_COUNTER_COMPARE_A,
                              pwmValueB); // Motor B PWM
}

// breathing_led.c
#include "util.h"
#include "assert.h" // from driverlib, gives ASSERT()
#include "device.h"
#include "driverlib.h"
#include "memcfg.h"
#include <math.h>
#include <stdint.h> // uintptr_t
#include <string.h> // memcpy, size_t

#define LED_TOGGLE_INTERVAL_MS 50   // 100 ms
#define LED_BREATHING_CYCLE_MS 2000 // 2 seconds for a full breathing cycle

// Configure CPU Timer 1 for free-running us counter
void initMicrosecondTimer(void) {
  // Stop the timer before configuration
  CPUTimer_stopTimer(CPUTIMER1_BASE);

  // No prescaler → timer ticks every SYSCLK (200 MHz = 5 ns/tick)
  CPUTimer_setPreScaler(CPUTIMER1_BASE, 0);

  // Period = max 32-bit
  CPUTimer_setPeriod(CPUTIMER1_BASE, 0xFFFFFFFF);

  // Reload and start
  CPUTimer_reloadTimerCounter(CPUTIMER1_BASE);
  CPUTimer_startTimer(CPUTIMER1_BASE);
}

// Return elapsed microseconds since timer started
uint32_t micros(void) {
  // CPU timers count down, so invert
  uint32_t ticks = 0xFFFFFFFF - CPUTimer_getTimerCount(CPUTIMER1_BASE);

  // Convert ticks → microseconds
  // 200 MHz = 200 ticks per 1 us
  return ticks / 200;
}

uint32_t millis(void) { return micros() / 1000; }

void statusLED_init(statusLED *led, uint32_t gpioPin) {
  led->gpioPin = gpioPin;
  led->timeStamp = 0;
  led->state = 1;    // start by lighting up
  led->blinking = 0; // not blinking initially
  led->enabled = 1;  // start enabled

  // GPIO setup
  GPIO_setPadConfig(led->gpioPin, GPIO_PIN_TYPE_STD);
  GPIO_setDirectionMode(led->gpioPin, GPIO_DIR_MODE_OUT);
}

// check the timer to update brightness; then implement soft pwm here to achieve
// breathing effect
void statusLED_breathe(statusLED *led) {
  uint32_t currentTimeMs = millis();
  if (!led->enabled) {
    GPIO_writePin(led->gpioPin, 1); // turn off
    led->state = 0;
    return;
  } else { // update brightness every 50ms
    led->timeStamp = currentTimeMs;
    // calculate brightness based on sine wave
    float phase =
        (2.0f * 3.14159265f * (currentTimeMs % LED_BREATHING_CYCLE_MS)) /
        LED_BREATHING_CYCLE_MS;
    float brightness =
        (sinf(phase - 3.14159265f / 2.0f) + 1.0f) / 2.0f; // normalize to [0,1]
    uint8_t dutyCycle = (uint8_t)(brightness * 255.0f);   // scale to [0,255]
    led->blinking = dutyCycle;

    // Implement soft PWM for breathing effect
    static uint8_t pwmCounter = 0;
    pwmCounter = (pwmCounter + 1) % 256; // cycle from 0 to 255
    if (pwmCounter < led->blinking) {
      GPIO_writePin(led->gpioPin, 0); // turn on
      led->state = 1;
    } else {
      GPIO_writePin(led->gpioPin, 1); // turn off
      led->state = 0;
    }
  }
}

void statusLED_blink(statusLED *led) {
  uint32_t currentTimeMs = millis();
  if (!led->enabled) {
    GPIO_writePin(led->gpioPin, 1); // turn off
    led->state = 0;
    return;
  } else if (currentTimeMs - led->timeStamp >=
             LED_TOGGLE_INTERVAL_MS) { // toggle every 100ms
    led->timeStamp = currentTimeMs;
    if (led->state) {
      GPIO_writePin(led->gpioPin, 0); // turn on
      led->state = 0;
    } else {
      GPIO_writePin(led->gpioPin, 1); // turn off
      led->state = 1;
    }
  }
}

void statusLED_toggle(statusLED *led) {
  if (!led->enabled) {
    GPIO_writePin(led->gpioPin, 1); // turn off
    led->state = 0;
    return;
  } else {
    if (led->state) {
      GPIO_writePin(led->gpioPin, 1); // turn off
      led->state = 0;
    } else {
      GPIO_writePin(led->gpioPin, 0); // turn off
      led->state = 1;
    }
  }
}

void disableStatusLED(statusLED *led) {
  led->enabled = 0;
  GPIO_writePin(led->gpioPin, 1); // turn off
  led->state = 0;
}

// === CLA related
void CLA_triggerFromTimer0_1kHz(void);
void CLA_triggerFromEPWM1_1kHz(void);

// ==== Symbols for Flash→LS copy (linker provides them) ====
extern uint16_t Cla1funcsLoadStart, Cla1funcsLoadSize, Cla1funcsRunStart;

// ==== CLA task symbol (implemented in control.cla) ====
void CLA_bringUp_1kHz(void) {
  // Override trigger to Timer0 instead of software
  CLA_setTriggerSource(CLA_TASK_1, CLA_TRIGGER_TINT0);

  // Configure Timer0 for 1 kHz and start it
  CLA_triggerFromTimer0_1kHz();
}

// Configure CPU Timer0 at 1 kHz and route it to CLA Task 1
void CLA_triggerFromTimer0_1kHz(void) {
  CPUTimer_stopTimer(CPUTIMER0_BASE);
  CPUTimer_setPeriod(CPUTIMER0_BASE, 100000UL - 1UL); // 200 MHz -> 1ms
  CPUTimer_setPreScaler(CPUTIMER0_BASE, 0);
  CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
  CPUTimer_enableInterrupt(CPUTIMER0_BASE);
  EALLOW;
  CLA_setTriggerSource(CLA_TASK_1, CLA_TRIGGER_TINT0);
  EDIS;
  CPUTimer_startTimer(CPUTIMER0_BASE);
}

// Configure EPWM1 at 1 kHz and route it to CLA Task 1
void CLA_triggerFromEPWM1_1kHz(void) {
  // TBCLK = 100 MHz (CLKDIV=1, HSPCLKDIV=2)
  EPWM_setClockPrescaler(EPWM1_BASE, EPWM_CLOCK_DIVIDER_1,
                         EPWM_HSCLOCK_DIVIDER_2);
  EPWM_setTimeBaseCounterMode(EPWM1_BASE, EPWM_COUNTER_MODE_UP_DOWN);
  EPWM_setTimeBasePeriod(EPWM1_BASE, 25000U); // 100e6 / (2*1e3) = 50,000
  EPWM_setTimeBaseCounter(EPWM1_BASE, 0);

  EPWM_setInterruptSource(EPWM1_BASE, EPWM_INT_TBCTR_ZERO);
  EPWM_enableInterrupt(EPWM1_BASE);
  EPWM_setInterruptEventCount(EPWM1_BASE, 1);

  EALLOW;
  CLA_setTriggerSource(CLA_TASK_1, CLA_TRIGGER_EPWM1INT);
  EDIS;
}

/*---------------------------------------------------------------------------
 *  ADC_init()
 *
 *  Configures ADC modules A, B, C, D for 12‑bit single‑ended operation.
 *  Each module uses SOC0 to sample one channel:
 *    ADCA: ADCINA3  → SOC0
 *    ADCB: ADCINB3  → SOC0
 *    ADCC: ADCINC3  → SOC0
 *    ADCD: ADCIND3  → SOC0
 *  Each SOC is software‑triggered and uses a sample window of 15 SYSCLKs.
 *  Interrupt 1 of each module is configured to fire at the end of SOC0.
 *
 *  Call this once during system initialisation (after Device_init).
 *--------------------------------------------------------------------------*/
void ADC_init(void) {
  /* Prescale ADC clocks to /4.  Other values (1/2/6/8) are available.    */
  ADC_setPrescaler(ADCA_BASE, ADC_CLK_DIV_4_0);
  ADC_setPrescaler(ADCB_BASE, ADC_CLK_DIV_4_0);
  ADC_setPrescaler(ADCC_BASE, ADC_CLK_DIV_4_0);
  ADC_setPrescaler(ADCD_BASE, ADC_CLK_DIV_4_0);

  /* Set 12‑bit resolution and single‑ended mode for all modules.          */
  ADC_setMode(ADCA_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
  ADC_setMode(ADCB_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
  ADC_setMode(ADCC_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
  ADC_setMode(ADCD_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);

  /* Generate interrupt pulse at end of conversion (vs. before).           */
  ADC_setInterruptPulseMode(ADCA_BASE, ADC_PULSE_END_OF_CONV);
  ADC_setInterruptPulseMode(ADCB_BASE, ADC_PULSE_END_OF_CONV);
  ADC_setInterruptPulseMode(ADCC_BASE, ADC_PULSE_END_OF_CONV);
  ADC_setInterruptPulseMode(ADCD_BASE, ADC_PULSE_END_OF_CONV);

  /* Power up all converters; wait ~1 ms for reference to settle.          */
  ADC_enableConverter(ADCA_BASE);
  ADC_enableConverter(ADCB_BASE);
  ADC_enableConverter(ADCC_BASE);
  ADC_enableConverter(ADCD_BASE);
  DEVICE_DELAY_US(1000);

  /*----------------------------------------------------------------------
   * Configure each SOC (Sample-and-Hold) channel.
   *   - ADC_trigger_SW_ONLY: conversions are started by software.
   *   - ADC_CH_ADCINx: selects which physical pin on that module.
   *   - Sample window of 15 SYSCLK cycles (~75 ns at 200 MHz).
   *
   * Note: Channel numbering (ADCIN3, ADCIN0, etc.) uses the same
   * enumeration across modules; the module base selects which ADC.
   *---------------------------------------------------------------------*/
  ADC_setupSOC(ADCA_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_SW_ONLY, ADC_CH_ADCIN3,
               15); // A3 → SOC0

  ADC_setupSOC(ADCB_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_SW_ONLY, ADC_CH_ADCIN3,
               15); // B3 → SOC0

  ADC_setupSOC(ADCC_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_SW_ONLY, ADC_CH_ADCIN3,
               15); // C3 → SOC0

  ADC_setupSOC(ADCD_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_SW_ONLY, ADC_CH_ADCIN3,
               15); // D3 → SOC0

  /* Each module’s SOC0 will set INT1 when the conversion completes.      */
  ADC_setInterruptSource(ADCA_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
  ADC_enableInterrupt(ADCA_BASE, ADC_INT_NUMBER1);
  ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);

  ADC_setInterruptSource(ADCB_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
  ADC_enableInterrupt(ADCB_BASE, ADC_INT_NUMBER1);
  ADC_clearInterruptStatus(ADCB_BASE, ADC_INT_NUMBER1);

  ADC_setInterruptSource(ADCC_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
  ADC_enableInterrupt(ADCC_BASE, ADC_INT_NUMBER1);
  ADC_clearInterruptStatus(ADCC_BASE, ADC_INT_NUMBER1);

  ADC_setInterruptSource(ADCD_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
  ADC_enableInterrupt(ADCD_BASE, ADC_INT_NUMBER1);
  ADC_clearInterruptStatus(ADCD_BASE, ADC_INT_NUMBER1);
}

/*---------------------------------------------------------------------------
 *  ADC_sampleTick()
 *
 *  Starts conversions on all four ADC modules, waits for completion, clears
 *  interrupt flags, reads raw counts, and converts them to voltages.  The
 *  voltages are stored in the shared CLA→CPU structure gClaToCpu.  This
 *  function may be called periodically from the main loop to update the
 *  analog inputs.
 *
 *  Order of results (in gClaToCpu):
 *    a_1 ← ADCIND0 (module D, pin IND0)
 *    a_2 ← ADCINA3 (module A, pin INA3)
 *    a_3 ← ADCINB3 (module B, pin INB3)
 *    a_4 ← ADCINC3 (module C, pin INC3)
 *--------------------------------------------------------------------------*/
extern uint16_t gAnalogIn[4]; // defined in main.c
void ADC_sampleTick(void) {
  /* Start all four conversions.                                           */
  ADC_forceSOC(ADCA_BASE, ADC_SOC_NUMBER0);
  ADC_forceSOC(ADCB_BASE, ADC_SOC_NUMBER0);
  ADC_forceSOC(ADCC_BASE, ADC_SOC_NUMBER0);
  ADC_forceSOC(ADCD_BASE, ADC_SOC_NUMBER0);

  /* Wait for each module’s INT1 flag and clear it.                        */
  while (!ADC_getInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1)) {
  }
  ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);

  while (!ADC_getInterruptStatus(ADCB_BASE, ADC_INT_NUMBER1)) {
  }
  ADC_clearInterruptStatus(ADCB_BASE, ADC_INT_NUMBER1);

  while (!ADC_getInterruptStatus(ADCC_BASE, ADC_INT_NUMBER1)) {
  }
  ADC_clearInterruptStatus(ADCC_BASE, ADC_INT_NUMBER1);

  while (!ADC_getInterruptStatus(ADCD_BASE, ADC_INT_NUMBER1)) {
  }
  ADC_clearInterruptStatus(ADCD_BASE, ADC_INT_NUMBER1);

  /* Read raw conversion results from each module’s result register        */
  gAnalogIn[0] = ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER0);
  gAnalogIn[1] = ADC_readResult(ADCBRESULT_BASE, ADC_SOC_NUMBER0);
  gAnalogIn[2] = ADC_readResult(ADCCRESULT_BASE, ADC_SOC_NUMBER0);
  gAnalogIn[3] = ADC_readResult(ADCDRESULT_BASE, ADC_SOC_NUMBER0);
}

void ADC_sampleTick_C2_C3_D2_D3(void) {
  /* Start all four conversions.                                           */
  ADC_forceSOC(ADCC_BASE, ADC_SOC_NUMBER0);
  ADC_forceSOC(ADCD_BASE, ADC_SOC_NUMBER0);

  while (!ADC_getInterruptStatus(ADCC_BASE, ADC_INT_NUMBER1)) {
  }
  ADC_clearInterruptStatus(ADCC_BASE, ADC_INT_NUMBER1);

  while (!ADC_getInterruptStatus(ADCD_BASE, ADC_INT_NUMBER1)) {
  }
  ADC_clearInterruptStatus(ADCD_BASE, ADC_INT_NUMBER1);

  /* Read raw conversion results from each module’s result register        */
  gAnalogIn[0] = ADC_readResult(ADCCRESULT_BASE, ADC_SOC_NUMBER2);
  gAnalogIn[1] = ADC_readResult(ADCCRESULT_BASE, ADC_SOC_NUMBER3);
  gAnalogIn[2] = ADC_readResult(ADCDRESULT_BASE, ADC_SOC_NUMBER2);
  gAnalogIn[3] = ADC_readResult(ADCDRESULT_BASE, ADC_SOC_NUMBER3);
}

/*---------------------------------------------------------------------------
 * Combined initialisation entry point
 *
 * Call this from main() after Encoders_init() and Board_init().  It
 * prepares the H‑bridge PWM modules, enable lines and the ADC channels
 * required by the CLA.
 *--------------------------------------------------------------------------*/

inline float unpack_float(uint16_t lo, uint16_t hi) {
  union {
    uint32_t u;
    float f;
  } conv;

  conv.u = ((uint32_t)hi << 16) | (uint32_t)lo;
  return conv.f;
}

inline void pack_float(float f, volatile uint16_t *lo, volatile uint16_t *hi) {
  union {
    float f;
    uint32_t u;
  } conv;

  conv.f = f;

  *lo = (uint16_t)(conv.u & 0xFFFFU);         // lower 16 bits
  *hi = (uint16_t)((conv.u >> 16) & 0xFFFFU); // upper 16 bits
}

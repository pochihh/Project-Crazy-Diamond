#ifndef UTIL_H
#define UTIL_H
#include "device.h"
#include <stdint.h>

#define STATUS_LED_GPIO DEVICE_GPIO_PIN_LED1

typedef struct {
  float ref_1;
  float ref_2;
  uint32_t pos_1;
  uint32_t pos_2;
} CpuToClaMsg;

typedef struct {
  float ref_1;
  float ref_2;
  uint32_t pos_1;
  uint32_t pos_2;
  float u_1;
  float u_2;
  uint32_t cycleCount;
} ClaToCpuMsg;

extern volatile CpuToClaMsg gCpuToCla;
extern volatile ClaToCpuMsg gClaToCpu;

// Configure CPU Timer 1 for free-running us counter
void initMicrosecondTimer(void);

// Return elapsed microseconds since timer started
uint32_t micros(void);

uint32_t millis(void);

typedef struct {
  uint32_t gpioPin;   // e.g., 12 for GPIO12 → EPWM7A
  uint32_t timeStamp; // using cpu timer to track time and toggle LED
  uint8_t state;      // 0 = lights up, 1 = down
  uint8_t blinking;   // 0~255 (duty cycle)
  uint8_t enabled;    // 1 = enabled, 0 = disabled
} statusLED;

void statusLED_init(statusLED *led, uint32_t gpioPin);
void statusLED_blink(statusLED *led);     // blink LED with designed frequency
void statusLED_blinkOnce(statusLED *led); // single blink with fixed duration.
                                          // No-op if already blinking
void statusLED_toggle(statusLED *led);    // toggle LED state
void enableStatusLED(statusLED *led);
void disableStatusLED(statusLED *led);

// Motor control analog-in reading
// === CLA related
void CLA_bringUp_1kHz(void);

// ADC and H-bridge

void ADC_init(void);
void ADC_sampleTick(void);


// MISC
float unpack_float(uint16_t lo, uint16_t hi);
void pack_float(float f, volatile uint16_t *lo, volatile uint16_t *hi);

#endif // UTIL_H

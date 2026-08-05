#include "encoders.h"

// ===== Pin selection (LaunchXL-F28379D defaults) =====
// EQEP1 on GPIO20 (A), GPIO21 (B), GPIO23 (I optional)
// EQEP2 on GPIO54 (A), GPIO55 (B), GPIO57 (I optional)
#define EQEP1_GPIO_A 20U
#define EQEP1_GPIO_B 21U
#define EQEP1_GPIO_I 23U

#define EQEP2_GPIO_A 54U
#define EQEP2_GPIO_B 55U
#define EQEP2_GPIO_I 57U

// Set to 1 if you want position reset on index pulse
#ifndef USE_EQEP1_INDEX_RESET
#define USE_EQEP1_INDEX_RESET 0
#endif
#ifndef USE_EQEP2_INDEX_RESET
#define USE_EQEP2_INDEX_RESET 0
#endif

#define QEPSTS_CLEARABLE_MASK                                                  \
  (EQEP_QEPSTS_PCEF | EQEP_QEPSTS_FIMF | EQEP_QEPSTS_CDEF | EQEP_QEPSTS_COEF | \
   EQEP_QEPSTS_QDLF | EQEP_QEPSTS_FIDF | EQEP_QEPSTS_UPEVNT)

// Public variables
volatile int32_t encoder1_position = 0;
volatile int32_t encoder2_position = 0;

volatile uint32_t encoder1_error_count = 0;
volatile uint32_t encoder2_error_count = 0;

EncoderStatusLog encoder1_log;
EncoderStatusLog encoder2_log;

// Internal variables for error counting
static uint32_t s_prev_error_count1 = 0U;
static uint32_t s_prev_error_count2 = 0U;

static inline void EQEPx_gpioSetup(uint32_t gpioA, uint32_t gpioB,
                                   int8_t hasIndex) {
  GPIO_setDirectionMode(gpioA, GPIO_DIR_MODE_IN);
  GPIO_setDirectionMode(gpioB, GPIO_DIR_MODE_IN);
  GPIO_setPadConfig(gpioA, GPIO_PIN_TYPE_STD);
  GPIO_setPadConfig(gpioB, GPIO_PIN_TYPE_STD);
  GPIO_setQualificationMode(gpioA, GPIO_QUAL_ASYNC);
  GPIO_setQualificationMode(gpioB, GPIO_QUAL_ASYNC);

  if (hasIndex) {
    uint32_t gpioI = (gpioA == EQEP1_GPIO_A) ? EQEP1_GPIO_I : EQEP2_GPIO_I;
    GPIO_setDirectionMode(gpioI, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(gpioI, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(gpioI, GPIO_QUAL_ASYNC);
  }

  // Pin mux
  if (gpioA == EQEP1_GPIO_A) {
    GPIO_setPinConfig(GPIO_20_EQEP1A);
    GPIO_setPinConfig(GPIO_21_EQEP1B);
    if (hasIndex)
      GPIO_setPinConfig(GPIO_23_EQEP1I);
  } else {
    // EQEP2
    GPIO_setPinConfig(GPIO_54_EQEP2A);
    GPIO_setPinConfig(GPIO_55_EQEP2B);
    if (hasIndex)
      GPIO_setPinConfig(GPIO_57_EQEP2I);
  }
}
// ===== Input qualification helpers =========================================

static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
  return (v < lo ? lo : (v > hi ? hi : v));
}

void Encoders_applyQualification(const uint32_t *pins, uint16_t num_pins,
                                 EncoderQualMode mode, uint32_t f_qual_hz) {
  if (pins == NULL || num_pins == 0)
    return;

  uint16_t i;

  // Map our enum to driverlib constants
  uint32_t qual_mode = GPIO_QUAL_ASYNC;
  switch (mode) {
  case ENCODER_QUAL_ASYNC:
    qual_mode = GPIO_QUAL_ASYNC;
    break;
  case ENCODER_QUAL_3SAMPLE:
    qual_mode = GPIO_QUAL_3SAMPLE;
    break;
  case ENCODER_QUAL_6SAMPLE:
    qual_mode = GPIO_QUAL_6SAMPLE;
    break;
  case ENCODER_QUAL_8SAMPLE:
#ifdef GPIO_QUAL_8SAMPLE
    qual_mode = GPIO_QUAL_8SAMPLE;
#else
    qual_mode = GPIO_QUAL_6SAMPLE; // graceful fallback if 8-sample not present
#endif
    break;
  default:
    qual_mode = GPIO_QUAL_ASYNC;
    break;
  }

  // For ASYNC, we just set mode and exit
  if (qual_mode == GPIO_QUAL_ASYNC || f_qual_hz == 0U) {
    for (i = 0; i < num_pins; ++i) {
      GPIO_setQualificationMode(pins[i], GPIO_QUAL_ASYNC);
    }
    return;
  }

  // Qualification clock:
  //   f_qual = SYSCLK / (2 * (n + 1))  -->  n = (SYSCLK / (2*f_qual)) - 1
  // We pick the closest integer n in [0, 255] (most devices use 8-bit field).
  const double denom = 2.0 * (double)f_qual_hz;
  uint32_t n = 0U;
  if (denom > 0.0) {
    double n_f = (double)DEVICE_SYSCLK_FREQ / denom - 1.0;
    // Round to nearest rather than floor for better fidelity
    n = (uint32_t)((n_f >= 0.0) ? (n_f + 0.5) : 0.0);
  }
  n = clamp_u32(n, 0U, 255U);

  for (i = 0; i < num_pins; ++i) {
    GPIO_setQualificationMode(pins[i], (GPIO_QualificationMode)qual_mode);
    GPIO_setQualificationPeriod(pins[i], (uint16_t)n);
  }
}

void Encoders_applyQualificationEQEP1(EncoderQualMode mode, uint32_t f_qual_hz,
                                      bool includeIndex) {
  uint32_t pins[3] = {EQEP1_GPIO_A, EQEP1_GPIO_B, EQEP1_GPIO_I};
  Encoders_applyQualification(pins, includeIndex ? 3 : 2, mode, f_qual_hz);
}

void Encoders_applyQualificationEQEP2(EncoderQualMode mode, uint32_t f_qual_hz,
                                      bool includeIndex) {
  uint32_t pins[3] = {EQEP2_GPIO_A, EQEP2_GPIO_B, EQEP2_GPIO_I};
  Encoders_applyQualification(pins, includeIndex ? 3 : 2, mode, f_qual_hz);
}

void Encoders_init(void) {
  // Enable peripheral clocks
  SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EQEP1);
  SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EQEP2);

  // Disable before configuration
  EQEP_disableModule(EQEP1_BASE);
  EQEP_disableModule(EQEP2_BASE);

  // GPIO setup + mux
  EQEPx_gpioSetup(EQEP1_GPIO_A, EQEP1_GPIO_B, USE_EQEP1_INDEX_RESET);
  EQEPx_gpioSetup(EQEP2_GPIO_A, EQEP2_GPIO_B, USE_EQEP2_INDEX_RESET);

  // Decoder config: quadrature, count both edges, no swap
  EQEP_setDecoderConfig(EQEP1_BASE,
                        (EQEP_CONFIG_QUADRATURE | EQEP_CONFIG_2X_RESOLUTION |
                         EQEP_CONFIG_NO_SWAP));
  EQEP_setDecoderConfig(EQEP2_BASE,
                        (EQEP_CONFIG_QUADRATURE | EQEP_CONFIG_2X_RESOLUTION |
                         EQEP_CONFIG_NO_SWAP));

  // Position counter behavior
#if USE_EQEP1_INDEX_RESET
  EQEP_setPositionCounterConfig(EQEP1_BASE, EQEP_POSITION_RESET_ON_INDEX,
                                0xFFFFFFFFU);
#else
  EQEP_setPositionCounterConfig(EQEP1_BASE, EQEP_POSITION_RESET_MAX_POS,
                                0xFFFFFFFFU);
#endif

#if USE_EQEP2_INDEX_RESET
  EQEP_setPositionCounterConfig(EQEP2_BASE, EQEP_POSITION_RESET_ON_INDEX,
                                0xFFFFFFFFU);
#else
  EQEP_setPositionCounterConfig(EQEP2_BASE, EQEP_POSITION_RESET_MAX_POS,
                                0xFFFFFFFFU);
#endif

  // Emulation: run free while halted
  EQEP_setEmulationMode(EQEP1_BASE, EQEP_EMULATIONMODE_RUNFREE);
  EQEP_setEmulationMode(EQEP2_BASE, EQEP_EMULATIONMODE_RUNFREE);

  // Clear any stale status
  EQEP_clearStatus(EQEP1_BASE, 0xFFFF);
  EQEP_clearStatus(EQEP2_BASE, 0xFFFF);

  // Enable modules
  EQEP_enableModule(EQEP1_BASE);
  EQEP_enableModule(EQEP2_BASE);

  // Initialize positions AFTER enabling
  EQEP_setInitialPosition(EQEP1_BASE, 0U);
  EQEP_setInitialPosition(EQEP2_BASE, 0U);
  EQEP_setPosition(EQEP1_BASE, 0U);
  EQEP_setPosition(EQEP2_BASE, 0U);

  encoder1_position = EQEP_getPosition(EQEP1_BASE);
  encoder2_position = EQEP_getPosition(EQEP2_BASE);
}

void Encoders_setPosition(uint32_t encoder1_pos, uint32_t encoder2_pos) {
  EQEP_setPosition(EQEP1_BASE, encoder1_pos);
  EQEP_setPosition(EQEP2_BASE, encoder2_pos);
}

void Encoders_zeroPosition(void) { Encoders_setPosition(0U, 0U); }

static inline void log_one(uint32_t base, EncoderStatusLog *log) {
  // -------- QFLG (event/interrupt flags) --------
  uint16_t flg = HWREGH(base + EQEP_O_QFLG);
  log->qflg_last = flg;

  if (flg & EQEP_QFLG_PHE)
    log->phase_error_count++;
  // Commented out to save code size - not actual errors
  // if (flg & EQEP_QFLG_QDC)
  //   log->direction_change_count++;
  // if (flg & EQEP_QFLG_PCU)
  //   log->underflow_count++;
  // if (flg & EQEP_QFLG_PCO)
  //   log->overflow_count++;
  // if (flg & EQEP_QFLG_UTO)
  //   log->timeout_count++;
  // if (flg & EQEP_QFLG_IEL)
  //   log->index_event_count++;
  // if (flg & EQEP_QFLG_SEL)
  //   log->strobe_event_count++;

  // Clear exactly what we saw in QFLG via QCLR
  if (flg)
    HWREGH(base + EQEP_O_QCLR) = flg;

  // -------- QEPSTS (status bits) --------
  uint16_t sts = HWREGH(base + EQEP_O_QEPSTS);
  log->qepsts_last = sts;

  // Mirror current direction (QDF)
  log->current_direction = (sts & EQEP_QEPSTS_QDF) ? 1u : 0u;

  if (sts & EQEP_QEPSTS_PCEF)
    log->position_error_count++;
  // Commented out to save code size - not actual errors
  // if (sts & EQEP_QEPSTS_QDLF)
  //   log->direction_latch_count++;
  // if (sts & EQEP_QEPSTS_UPEVNT)
  //   log->unit_position_count++;
  // if (sts & EQEP_QEPSTS_COEF)
  //   log->capture_overflow_count++;
  // if (sts & EQEP_QEPSTS_CDEF)
  //   log->capture_direction_error_count++;

  // Clear only clearable bits, leave QDF alone
  uint16_t to_clear = sts & QEPSTS_CLEARABLE_MASK;
  if (to_clear)
    HWREGH(base + EQEP_O_QEPSTS) = to_clear;
}

void Encoders_updatePosition(void) {
  encoder1_position = (int32_t)EQEP_getPosition(EQEP1_BASE);
  encoder2_position = (int32_t)EQEP_getPosition(EQEP2_BASE);

  // Log status and count errors
  log_one(EQEP1_BASE, &encoder1_log);
  log_one(EQEP2_BASE, &encoder2_log);
  
  // Update total error counts (only phase and position errors)
  // PHE: Quadrature phase error - indicates signal quality issues
  // PCEF: Position counter error - indicates counter overflow/underflow
  uint32_t current_errors1 = encoder1_log.phase_error_count + 
                             encoder1_log.position_error_count;
                             
  uint32_t current_errors2 = encoder2_log.phase_error_count + 
                             encoder2_log.position_error_count;
  
  encoder1_error_count += (current_errors1 - s_prev_error_count1);
  encoder2_error_count += (current_errors2 - s_prev_error_count2);
  
  s_prev_error_count1 = current_errors1;
  s_prev_error_count2 = current_errors2;
}

// Get current encoder positions
void Encoders_getPosition(int32_t *encoder1_pos, int32_t *encoder2_pos) {
  if (encoder1_pos) {
    *encoder1_pos = encoder1_position;
  }
  if (encoder2_pos) {
    *encoder2_pos = encoder2_position;
  }
}

// Get total error counts
void Encoders_getErrorCounts(uint32_t *encoder1_errors, uint32_t *encoder2_errors) {
  if (encoder1_errors) {
    *encoder1_errors = encoder1_error_count;
  }
  if (encoder2_errors) {
    *encoder2_errors = encoder2_error_count;
  }
}

#ifndef ENCODERS_H
#define ENCODERS_H

#include "device.h"
#include "driverlib.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Global variables for encoder position and error counts
extern volatile int32_t encoder1_position, encoder2_position;   // unit: count
extern volatile uint32_t encoder1_error_count, encoder2_error_count; // total error count

typedef struct {
  // Raw last snapshots (for debugging)
  volatile uint16_t qflg_last;
  volatile uint16_t qepsts_last;

  // Error counters (only the important ones)
  volatile uint32_t phase_error_count;    // Quadrature phase error (PHE) - signal quality issues
  volatile uint32_t position_error_count; // Position counter error (PCEF) - counter overflow/underflow
  
  // Commented out to save code size - not actual errors
  // volatile uint32_t direction_change_count;   // Direction change
  // volatile uint32_t underflow_count;    // Position counter underflow
  // volatile uint32_t overflow_count;     // Position counter overflow
  // volatile uint32_t timeout_count;      // Unit timeout
  // volatile uint32_t index_event_count;  // Index latch
  // volatile uint32_t strobe_event_count; // Strobe latch
  // volatile uint32_t direction_latch_count;    // Direction latch
  // volatile uint32_t unit_position_count;     // Unit position event
  // volatile uint32_t capture_overflow_count; // Capture overflow
  // volatile uint32_t capture_direction_error_count;  // Capture direction error

  // Current direction (QDF)
  volatile uint16_t current_direction;
} EncoderStatusLog;

// Global logs for encoder1 and encoder2
extern EncoderStatusLog encoder1_log;
extern EncoderStatusLog encoder2_log;

// Call this regularly to poll, log, and clear flags
void Encoders_logStatus(void);

// ------- General input qualification helpers --------------------------------

typedef enum {
  ENCODER_QUAL_ASYNC = 0, // No digital filtering (fastest)
  ENCODER_QUAL_3SAMPLE,   // 3-sample majority
  ENCODER_QUAL_6SAMPLE,   // 6-sample majority
  ENCODER_QUAL_8SAMPLE // 8-sample majority (if device supports; else falls back
                       // to 6)
} EncoderQualMode;

// Call once after Board_init()
void Encoders_init(void);

// Call periodically to update position and error counts
void Encoders_updatePosition(void);

// Reset the encoder position
void Encoders_setPosition(uint32_t encoder1_pos, uint32_t encoder2_pos);
void Encoders_zeroPosition(void);

/**
 * Apply input qualification to a list of GPIO pins.
 *
 * @param pins        Array of GPIO pin numbers (e.g., {20,21})
 * @param num_pins    How many pins in the array
 * @param mode        ENCODER_QUAL_ASYNC / 3SAMPLE / 6SAMPLE / 8SAMPLE
 * - ENCODER_QUAL_ASYNC
 *  - Asynchronous sampling (no filter).
 * - ENCODER_QUAL_3/6/8SAMPLE
 *  - Majority vote over 3/6/8 samples at the qualification clock rate
 *  - Rejects glitches shorter
 * @param f_qual_hz   Qualification sampling frequency target in Hz.
 *                    Ignored for ASYNC. The driver computes the nearest
 * prescaler. Typical starting point: 1–5 kHz for hand tests; 5–50 kHz for
 * faster systems.
 */
void Encoders_applyQualification(const uint32_t *pins, uint16_t num_pins,
                                 EncoderQualMode mode, uint32_t f_qual_hz);

/** Convenience: apply qualification to EQEP1 A/B (and I if includeIndex = true)
 */
void Encoders_applyQualificationEQEP1(EncoderQualMode mode, uint32_t f_qual_hz,
                                      bool includeIndex);

/** Convenience: apply qualification to EQEP2 A/B (and I if includeIndex = true)
 */
void Encoders_applyQualificationEQEP2(EncoderQualMode mode, uint32_t f_qual_hz,
                                      bool includeIndex);

// Get current encoder positions
void Encoders_getPosition(int32_t *encoder1_pos, int32_t *encoder2_pos);

// Get total error counts
void Encoders_getErrorCounts(uint32_t *encoder1_errors, uint32_t *encoder2_errors);

#ifdef __cplusplus
}
#endif

#endif // ENCODERS_H

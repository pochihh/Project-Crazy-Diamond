#ifndef MESSAGECENTER_H
#define MESSAGECENTER_H

#include "device.h"
#include "driverlib.h"
#include <stdbool.h>
#include <stdint.h>

// ---------------- SPIA pin selection (LaunchXL-F28379D default) -------------
// #define MC_SPIA_GPIO_SIMO 16U
// #define MC_SPIA_GPIO_SOMI 17U
// #define MC_SPIA_GPIO_SCLK 18U
// #define MC_SPIA_GPIO_STE  19U

#define MC_SPIA_GPIO_SIMO 58U
#define MC_SPIA_GPIO_SOMI 59U
#define MC_SPIA_GPIO_SCLK 60U
#define MC_SPIA_GPIO_STE 61U

// Use a separate GPIO that is wired to the same Pi CS to frame start/end.
// Pick your CS pin and XINT channel/vector
#define MC_CS_GPIO 123U               // your CS monitor pin
#define MC_CS_XINT_CH GPIO_INT_XINT1 // channel binding for that pin
#define MC_CS_INT_VECTOR INT_XINT1   // PIE vector for that channel

#define MC_RX_MAX_WORDS 64U

// ---------------- Frame format (16-bit little-endian words) ------------------
// [0]  : 0xA5A5   (magic)
// [1]  : 0x0002   (version)
// [2]  : payload_words  (number of words from [3] .. [last-1])
// [3]  : timestamp low16
// [4]  : timestamp high16
// CH1:
// [5]  : pos1 low16          (int32_t)
// [6]  : pos1 high16
// [7]  : speed1 low16        (float32, IEEE-754, FPU32)
// [8]  : speed1 high16
// [9]  : err1 low16          (uint32_t)
// [10] : err1 high16
// CH2:
// [11] : pos2 low16
// [12] : pos2 high16
// [13] : speed2 low16
// [14] : speed2 high16
// [15] : err2 low16
// [16] : err2 high16
// CheckSum:
// [17] : CRC16-CCITT over words [0..16], poly 0x1021, init 0xFFFF
//
// Total words = 18.

// For safety we keep a small margin in the DMA buffer:
#define MC_TX_MAX_WORDS 64U
#define MC_RX_MAX_WORDS 64U // kept for future RX command frames

// Optional RX callback if/when you start parsing inbound commands.
typedef void (*MC_RxCallback)(const uint16_t *rx_words, uint16_t rx_word_count,
                              bool overflow);

// Initialize SPIA (slave), CS monitor (XINT), and DMA (TX + RX)
void MessageCenter_init(void);

// Set optional RX-complete callback (can be NULL)
void MessageCenter_setRxCallback(MC_RxCallback cb);

// Build the TX frame for this cycle (header, len, ts, ch1, ch2, crc).
// Call this once per “frame” (e.g., right after you computed positions/speeds).
void MessageCenter_tick(uint32_t timestamp_us, int32_t pos1, float speed1_cps,
                        uint32_t err1, int32_t pos2, float speed2_cps,
                        uint32_t err2);

// (Optional) Pause/resume TX DMA arming at CS falling edge
void MessageCenter_pauseTx(bool pause);

#endif // MESSAGECENTER_H

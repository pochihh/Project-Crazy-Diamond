#ifndef MESSAGE_CENTER_H
#define MESSAGE_CENTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------- TX frame (DSP -> RPi) ----------
typedef struct {
  uint32_t timestamp_us;
  float ref_1, ref_2; // duty cycle: -1.0-1.0
  int32_t pos_1, pos_2; // encoder feedback
  uint16_t err_1, err_2;
  float u_1, u_2; // control effort; for now's it's the same as ref_1 and ref_2
  uint16_t analogIn[4]; // analog input readings
} TxFrame_t;


#define MAX_FRAME_WORDS 30 // Maximum frame size for buffers

// TX frame: 
// Header: 0xAA55 (2 bytes)
// Version: 0x0001 (2 bytes)
// timestamp_us: 4 bytes
// ref_1: 4 bytes
// ref_2: 4 bytes
// pos_1: 4 bytes
// pos_2: 4 bytes
// err_1: 2 bytes
// err_2: 2 bytes
// u_1: 4 bytes
// u_2: 4 bytes
// analogIn[4]: 8 bytes
// Total data: 4 + 2 + 4*5 + 2*2 + 4*2 + 2*4 = 44 bytes (24 words)
// Total tx data: 24 words + 1 word (CRC16) = 25 words
#define TX_FRAME_WORDS 23 // TX data size in 16-bit words (including CRC)

// ---------- RX frame (RPi -> DSP) ----------
typedef struct {
  uint32_t cmd;
  float ref1;
  float ref2;
} RxFrame_t;
// RX frame: 
// Header: 0x55AA (2 bytes)
// Version: 0x0001 (2 bytes)
// cmd: 4 bytes
// ref1: 4 bytes
// ref2: 4 bytes
// Total data: 2 + 2 + 3 * 4 = 16 bytes (8 words)
// Total rx data: 8 words + 1 word (CRC16) = 9 words
#define RX_FRAME_WORDS 9  // RX data size in 16-bit words (including CRC)

// callback functions
// input just the raw data, no parsing
typedef void (*MessageCenter_rxCallback)(const volatile uint16_t *in);

// Public API
void MessageCenter_init(void);
void MessageCenter_tick(
    const TxFrame_t *in); // push a new TX snapshot (or call provider)
bool MessageCenter_setRxCallback(MessageCenter_rxCallback fn);

bool MessageCenter_getLastRx(RxFrame_t *out);

#endif // MESSAGE_CENTER_H

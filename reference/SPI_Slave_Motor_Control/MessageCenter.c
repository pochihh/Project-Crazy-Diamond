#include "MessageCenter.h"
#include "device.h"
#include "driverlib.h"
#include "util.h"

// === Pin definitions for SPIA on F28379D (LaunchXL J2 header, pins 58–61) ===
#define DSP_SPI_GPIO_SIMO 58U       // Pi MOSI <-> DSP SIMO
#define DSP_SPI_GPIO_SOMI 59U       // Pi MISO <-> DSP SOMI
#define DSP_SPI_GPIO_CLK 60U        // Pi SCLK <-> DSP CLK
#define DSP_SPI_GPIO_STE 61U        // Pi CS   <-> DSP STE
#define DSP_SPI_GPIO_CS_MIRROR 123U // Mirrored CS pin for interrupt monitoring

#define DSP_SPI_PROTO SPI_PROT_POL1PHA1 // Mode 3

#define TX_FRAME_DUMMY 0xFFFF
#define TX_FRAME_NUM_DUMMY_WORDS 4
#define TX_FRAME_HEADER 0xAA55
#define TX_FRAME_VERSION 0x0002

static volatile uint16_t s_tx_bufferA[MAX_FRAME_WORDS];
static volatile uint16_t s_tx_bufferB[MAX_FRAME_WORDS];
static volatile uint16_t *s_txActive = s_tx_bufferA;
static volatile uint16_t *s_txStandby = s_tx_bufferB;
static volatile bool s_tx_streaming_active = false;
static volatile bool s_tx_data_ready = false;

static uint16_t s_rx_buffer[MAX_FRAME_WORDS];
static uint16_t s_rx_cmd_buffer[MAX_FRAME_WORDS];

static uint16_t s_rx_index = 0;
static uint16_t s_tx_index = 0;

static MessageCenter_rxCallback s_rxCallback = NULL;
static volatile RxFrame_t s_lastRxFrame;

static volatile uint16_t s_structErrorCount = 0;
static volatile uint16_t s_crcErrorCount = 0;
static volatile uint16_t s_rxFrameCount = 0;
static uint16_t s_debug_rx_isr_count = 0;
volatile uint16_t s_debug_not_tx_active_in_streaming = 0;

// === forward declaration ===
__interrupt static void rx_fifo_isr(void);
__interrupt static void cs_isr(void);

// === Ramfunc declarations ===
// Mark ALL critical functions as ramfuncs for maximum performance
#pragma CODE_SECTION(reset_spi_state, ".TI.ramfunc")
#pragma CODE_SECTION(prime_txBuffer, ".TI.ramfunc")
#pragma CODE_SECTION(stream_txBuffer, ".TI.ramfunc")
#pragma CODE_SECTION(swap_tx_buffers, ".TI.ramfunc")
#pragma CODE_SECTION(rx_fifo_isr, ".TI.ramfunc")
#pragma CODE_SECTION(cs_isr, ".TI.ramfunc")
#pragma CODE_SECTION(build_tx_frame, ".TI.ramfunc")
#pragma CODE_SECTION(parse_rx_frame, ".TI.ramfunc")
#pragma CODE_SECTION(MessageCenter_init, ".TI.ramfunc")
#pragma CODE_SECTION(MessageCenter_tick, ".TI.ramfunc")
#pragma CODE_SECTION(MessageCenter_setRxCallback, ".TI.ramfunc")
#pragma CODE_SECTION(MessageCenter_getLastRx, ".TI.ramfunc")

// === Internal helpers ===
static inline uint16_t lo16(uint32_t v) { return (uint16_t)(v & 0xFFFFu); }
static inline uint16_t hi16(uint32_t v) { return (uint16_t)(v >> 16); }

static inline uint16_t CRC16(volatile uint16_t *w, uint16_t size) {
  uint16_t i, j;
  uint16_t crc = 0xFFFF;
  for (i = 0; i < size; ++i) {
    crc ^= w[i];
    for (j = 0; j < 16; j++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
  }
  return crc;
}

// Safe buffer swap - only when NOT transmitting
static inline void swap_tx_buffers(void) {
  // Only swap if we're not currently transmitting
  if (!s_tx_streaming_active) {
    volatile uint16_t *t = s_txActive;
    s_txActive = s_txStandby;
    s_txStandby = t;
    s_tx_data_ready = false;
  }
}

// Comprehensive SPI state reset for long intervals
static inline void reset_spi_state(void) {
  // Disable interrupts during reset
  DINT;

  // Reset all SPI state variables
  s_tx_streaming_active = false;
  s_tx_data_ready = false;
  s_rx_index = 0;
  s_tx_index = 0;

  // Reset SPI hardware
  SPI_disableModule(SPIA_BASE);
  SPI_resetTxFIFO(SPIA_BASE);
  SPI_resetRxFIFO(SPIA_BASE);
  SPI_clearInterruptStatus(SPIA_BASE, SPI_INT_RXFF | SPI_INT_TXFF);
  SPI_enableModule(SPIA_BASE);

  // Re-enable interrupts
  EINT;
}

// Prime the tx buffer with the header and version
static inline void prime_txBuffer(void) {
  // Reset the TX FIFO
  SPI_resetTxFIFO(SPIA_BASE);

  // Wait for FIFO to be empty before priming
  while (SPI_getTxFIFOStatus(SPIA_BASE) != SPI_FIFO_TXEMPTY) {
  }

  // Prime with words of dummy frams, then the header and version
  int i;
  for (i = 0; i < TX_FRAME_NUM_DUMMY_WORDS; i++) {
    HWREGH(SPIA_BASE + SPI_O_TXBUF) = TX_FRAME_DUMMY;
  }
  HWREGH(SPIA_BASE + SPI_O_TXBUF) = TX_FRAME_HEADER;
  HWREGH(SPIA_BASE + SPI_O_TXBUF) = TX_FRAME_VERSION;
  s_tx_index = i + 2;
  return;
}

// Stream all the words from the tx buffer as long as the FIFO is not full
// return the number of words actually written to the FIFO
static inline uint16_t stream_txBuffer(void) {
  if (!s_tx_streaming_active) {
    s_debug_not_tx_active_in_streaming++;
    return 0;
  }

  uint16_t sizeSent = 0;
  // Stream from active buffer (safe - no one else touches it during
  // transmission)
  while (s_tx_index < TX_FRAME_NUM_DUMMY_WORDS + TX_FRAME_WORDS &&
         SPI_getTxFIFOStatus(SPIA_BASE) != SPI_FIFO_TXFULL) {
    // Send data from active buffer
    uint16_t data = s_txActive[s_tx_index++];
    HWREGH(SPIA_BASE + SPI_O_TXBUF) = data;
    sizeSent++;
  }
  return sizeSent;
}

static void build_tx_frame(const TxFrame_t *in) {
  // Validate input
  if (!in)
    return;

  // build the tx frame in standby buffer (safe - not being transmitted)
  int i = 0;
  // fill with dummy words
  for (i = 0; i < TX_FRAME_NUM_DUMMY_WORDS; i++) {
    s_txStandby[i] = TX_FRAME_DUMMY;
  }
  // add the header and version
  s_txStandby[i++] = TX_FRAME_HEADER;  // Header
  s_txStandby[i++] = TX_FRAME_VERSION; // Version
  // timestamp_us
  s_txStandby[i++] = lo16(in->timestamp_us);
  s_txStandby[i++] = hi16(in->timestamp_us);

  // ref_1
  pack_float(in->ref_1, &s_txStandby[i], &s_txStandby[i + 1]);
  i += 2;
  // ref_2
  pack_float(in->ref_2, &s_txStandby[i], &s_txStandby[i + 1]);
  i += 2;

  // pos_1
  s_txStandby[i++] = lo16(in->pos_1);
  s_txStandby[i++] = hi16(in->pos_1);
  // pos_2
  s_txStandby[i++] = lo16(in->pos_2);
  s_txStandby[i++] = hi16(in->pos_2);
  // err_1
  s_txStandby[i++] = in->err_1;
  // err_2
  s_txStandby[i++] = in->err_2;

  // u_1
  pack_float(in->u_1, &s_txStandby[i], &s_txStandby[i + 1]);
  i += 2;
  // u_2
  pack_float(in->u_2, &s_txStandby[i], &s_txStandby[i + 1]);
  i += 2;
  // analogIn[4]
  s_txStandby[i++] = in->analogIn[0];
  s_txStandby[i++] = in->analogIn[1];
  s_txStandby[i++] = in->analogIn[2];
  s_txStandby[i++] = in->analogIn[3];
  // CRC16 - calculate over the data in the standby buffer
  s_txStandby[i++] =
      CRC16(s_txStandby + TX_FRAME_NUM_DUMMY_WORDS, TX_FRAME_WORDS - 1);

  // padding
  while (i < MAX_FRAME_WORDS) {
    s_txStandby[i++] = 0;
  }
}

// parse the rx buffer into the lastRxFrame struct
static bool parse_rx_frame(void) {
  // There might be dummy words at the beginning of the buffer, so we need to
  // find the header
  int i = 0;
  while (s_rx_buffer[i] != 0x55AA) {
    i++;
    if (i >= MAX_FRAME_WORDS) {
      return false; // no header found
    }
  }

  // basic checks
  if (s_rx_buffer[i] != 0x55AA) {
    s_structErrorCount++;
    return false; // bad header
  }
  if (s_rx_buffer[i + 1] != 0x0001) {
    s_structErrorCount++;
    return false; // bad version
  }
  if (s_rx_index < RX_FRAME_WORDS) {
    // padding bytes are ignored
    s_structErrorCount++;
    return false; // incomplete frame
  }
  // CRC check
  uint16_t crc = CRC16(s_rx_buffer + i, RX_FRAME_WORDS - 1);
  if (crc != s_rx_buffer[i + RX_FRAME_WORDS - 1]) {
    s_crcErrorCount++;
    return false; // bad CRC
  }

  // copy the command buffer
  memcpy(s_rx_cmd_buffer, s_rx_buffer + i + 2,
         sizeof(uint16_t) * (RX_FRAME_WORDS - 2));

  // call the callback if set
  if (s_rxCallback) {
    s_rxCallback(s_rx_cmd_buffer);
  }

  return true;
}

static void SPI_init(void) {
  // === Pinmux ===
  GPIO_setPinConfig(GPIO_58_SPISIMOA);
  GPIO_setPinConfig(GPIO_59_SPISOMIA);
  GPIO_setPinConfig(GPIO_60_SPICLKA);
  GPIO_setPinConfig(GPIO_61_SPISTEA);
  GPIO_setPinConfig(GPIO_123_GPIO123);

  GPIO_setDirectionMode(DSP_SPI_GPIO_SIMO, GPIO_DIR_MODE_OUT);
  GPIO_setDirectionMode(DSP_SPI_GPIO_SOMI, GPIO_DIR_MODE_IN);
  GPIO_setDirectionMode(DSP_SPI_GPIO_CLK, GPIO_DIR_MODE_IN);
  GPIO_setDirectionMode(DSP_SPI_GPIO_STE, GPIO_DIR_MODE_IN);
  GPIO_setDirectionMode(DSP_SPI_GPIO_CS_MIRROR, GPIO_DIR_MODE_IN);

  // GPIO_setPadConfig(DSP_SPI_GPIO_SIMO, GPIO_PIN_TYPE_PULLUP);
  GPIO_setPadConfig(DSP_SPI_GPIO_CLK, GPIO_PIN_TYPE_PULLUP);
  GPIO_setPadConfig(DSP_SPI_GPIO_STE, GPIO_PIN_TYPE_PULLUP);
  GPIO_setPadConfig(DSP_SPI_GPIO_CS_MIRROR, GPIO_PIN_TYPE_PULLUP);

  // Qualification Mode for clean CS timing (no sync/qual delays):
  GPIO_setQualificationMode(DSP_SPI_GPIO_STE, GPIO_QUAL_ASYNC);
  GPIO_setQualificationMode(DSP_SPI_GPIO_CS_MIRROR, GPIO_QUAL_ASYNC);

  // === SPI Config ===
  SPI_disableModule(SPIA_BASE);

  SPI_setConfig(SPIA_BASE, DEVICE_LSPCLK_FREQ,
                // SPI_PROT_POL0PHA0, // Mode 0
                DSP_SPI_PROTO,
                SPI_MODE_SLAVE, // Slave mode
                1000000,        // Baudrate ignored in slave
                16);            // 16-bit words
  SPI_enableTalk(SPIA_BASE);
  SPI_enableFIFO(SPIA_BASE);
  SPI_resetTxFIFO(SPIA_BASE);
  SPI_resetRxFIFO(SPIA_BASE);

  // === Setup RX interrupt ===
  SPI_setFIFOInterruptLevel(SPIA_BASE, SPI_FIFO_TX1, SPI_FIFO_RX1);
  SPI_clearInterruptStatus(SPIA_BASE, SPI_INT_RXFF | SPI_INT_TXFF);

  SPI_enableInterrupt(SPIA_BASE, SPI_INT_RXFF);
  SPI_disableInterrupt(SPIA_BASE, SPI_INT_TXFF);

  Interrupt_register(INT_SPIA_RX, &rx_fifo_isr);
  Interrupt_enable(INT_SPIA_RX);

  SPI_enableModule(SPIA_BASE);

  // === Setup CS interrupt (XINT1 on GPIO123 - mirrored CS pin) ===
  GPIO_setInterruptPin(DSP_SPI_GPIO_CS_MIRROR, GPIO_INT_XINT1);
  GPIO_setInterruptType(GPIO_INT_XINT1, GPIO_INT_TYPE_BOTH_EDGES);
  GPIO_enableInterrupt(GPIO_INT_XINT1);

  Interrupt_register(INT_XINT1, &cs_isr);
  Interrupt_enable(INT_XINT1);
}

// === Public API ===
void MessageCenter_init(void) {
  uint16_t i;
  // Initialize buffers and state
  for (i = 0; i < MAX_FRAME_WORDS; ++i) {
    s_tx_bufferA[i] = 0;
    s_tx_bufferB[i] = 0;
    s_rx_buffer[i] = 0;
  }
  s_rx_index = 0;
  s_tx_index = 0;
  s_tx_streaming_active = false;
  s_tx_data_ready = false;
  s_rxCallback = NULL;
  s_structErrorCount = 0;
  s_crcErrorCount = 0;

  SPI_init();

  prime_txBuffer();
}

void MessageCenter_tick(const TxFrame_t *in) {
  if (in && !s_tx_streaming_active) {
    // Build frame in standby buffer (safe - not being transmitted)
    build_tx_frame(in);

    // Mark data as ready for next transmission
    s_tx_data_ready = true;

    // Try to swap buffers if not currently transmitting
    swap_tx_buffers();
  }
}

bool MessageCenter_setRxCallback(MessageCenter_rxCallback fn) {
  // Set the RX callback function
  if (fn) {
    s_rxCallback = fn;
    return true;
  }
  return false;
}

bool MessageCenter_getLastRx(RxFrame_t *out) {
  if (out) {
    *out = s_lastRxFrame;
    return true;
  }
  return false;
}

// === RX ISR: new data in RX FIFO ===
__interrupt static void rx_fifo_isr(void) {
  s_debug_rx_isr_count++;

  while (SPI_getRxFIFOStatus(SPIA_BASE) != SPI_FIFO_RXEMPTY) {
    uint16_t rx_word = HWREGH(SPIA_BASE + SPI_O_RXBUF);
    if (s_rx_index < MAX_FRAME_WORDS) {
      s_rx_buffer[s_rx_index++] = rx_word;
    }
  }

  // Stream as many data to tx as possible (only if streaming is active)
  if (s_tx_streaming_active) {
    stream_txBuffer();
  }

  // clear interrupt flag
  SPI_clearInterruptStatus(SPIA_BASE, SPI_INT_RXFF);
  Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP6);
}

// === CS ISR: CS goes low, a new transfer is starting ===
__interrupt static void cs_isr(void) {
  DINT;
  if (GPIO_readPin(DSP_SPI_GPIO_CS_MIRROR) == 0) {
    // CS ISR: CS goes low, a new transfer is starting
    s_tx_streaming_active = true;

    // Start streaming with current buffer
    stream_txBuffer();

    s_debug_rx_isr_count = 0;
  } else {
    // CS goes high, a frame ended
    // parse the rx frame
    parse_rx_frame();

    // Now it's safe to swap buffers if new data is ready
    if (s_tx_data_ready) {
      swap_tx_buffers();
    }

    // Use comprehensive reset to handle long intervals
    reset_spi_state();
    prime_txBuffer();
  }
  Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
  EINT;
}

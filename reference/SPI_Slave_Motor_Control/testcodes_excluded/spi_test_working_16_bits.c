#include "device.h"
#include "driverlib.h"

// === Pin definitions for SPIA on F28379D (LaunchXL J2 header, pins 58–61) ===
#define DSP_SPI_GPIO_SIMO 58U // Pi MOSI <-> DSP SIMO
#define DSP_SPI_GPIO_SOMI 59U // Pi MISO <-> DSP SOMI
#define DSP_SPI_GPIO_CLK 60U  // Pi SCLK <-> DSP CLK
#define DSP_SPI_GPIO_STE 61U  // Pi CS   <-> DSP STE

//! Mode 0. Polarity 0, phase 0. Rising edge without delay.
#define SPI_MODE_0 SPI_PROT_POL0PHA0
//! Mode 1. Polarity 0, phase 1. Rising edge with delay.
#define SPI_MODE_1 SPI_PROT_POL0PHA1
//! Mode 2. Polarity 1, phase 0. Falling edge without delay.
#define SPI_MODE_2 SPI_PROT_POL1PHA0
//! Mode 3. Polarity 1, phase 1. Falling edge with delay.
#define SPI_MODE_3 SPI_PROT_POL1PHA1

// choose ONE mode that matches the Pi:
#define DSP_SPI_PROTO SPI_MODE_3

#define FRAME_WORDS 16

static volatile uint16_t s_tx_buffer[FRAME_WORDS];
static volatile uint16_t s_rx_buffer[FRAME_WORDS];
static volatile uint16_t s_rx_index = 0;

static void fill_tx_pattern(void) {
  s_tx_buffer[0] = 0x0001;
  s_tx_buffer[1] = 0x0002;
  s_tx_buffer[2] = 0x0004;
  s_tx_buffer[3] = 0x0008;

  s_tx_buffer[4] = 0x0010;
  s_tx_buffer[5] = 0x0020;
  s_tx_buffer[6] = 0x0040;
  s_tx_buffer[7] = 0x0080;

  s_tx_buffer[8] = 0x0100;
  s_tx_buffer[9] = 0x0200;
  s_tx_buffer[10] = 0x0400;
  s_tx_buffer[11] = 0x0800;

  s_tx_buffer[12] = 0x1000;
  s_tx_buffer[13] = 0x2000;
  s_tx_buffer[14] = 0x4000;
  s_tx_buffer[15] = 0x8000;
}

// Clear RX overflow and reset RX FIFO
static inline void SPI_clearRxOverflowAndReset(uint32_t base) {
  // 1) Clear RX overflow: write-1-to-clear RXFFOVF (bit 6) in SPIFFRX
  //    (driverlib doesn’t expose a wrapper for this bit)
  HWREGH(base + SPI_O_FFRX) |= (uint16_t)(1U << 6);

  // 2) Reset RX FIFO (driverlib wrapper toggles RXFIFORESET)
  SPI_resetRxFIFO(base);
}

// Forward declarations
static void spi_test_init(void);
__interrupt static void cs_isr(void);
__interrupt static void rx_fifo_isr(void);

void main(void) {
  Device_init();
  Device_initGPIO();
  Interrupt_initModule();
  Interrupt_initVectorTable();

  spi_test_init();
  uint16_t i;
  for (i = 0; i < FRAME_WORDS; ++i) {
    s_tx_buffer[i] = 0;
  }
  fill_tx_pattern();

  EINT; // Enable global interrupt
  ERTM; // Enable real-time debug

  for (;;) {
    // Idle loop
  }
}

static void spi_test_init(void) {
  // === Pinmux ===
  GPIO_setPinConfig(GPIO_58_SPISIMOA);
  GPIO_setPinConfig(GPIO_59_SPISOMIA);
  GPIO_setPinConfig(GPIO_60_SPICLKA);
  GPIO_setPinConfig(GPIO_61_SPISTEA);

  GPIO_setDirectionMode(DSP_SPI_GPIO_SIMO, GPIO_DIR_MODE_IN);
  GPIO_setDirectionMode(DSP_SPI_GPIO_SOMI, GPIO_DIR_MODE_OUT);
  GPIO_setDirectionMode(DSP_SPI_GPIO_CLK, GPIO_DIR_MODE_IN);
  GPIO_setDirectionMode(DSP_SPI_GPIO_STE, GPIO_DIR_MODE_IN);

  GPIO_setPadConfig(DSP_SPI_GPIO_SIMO, GPIO_PIN_TYPE_PULLUP);
  GPIO_setPadConfig(DSP_SPI_GPIO_CLK, GPIO_PIN_TYPE_PULLUP);
  GPIO_setPadConfig(DSP_SPI_GPIO_STE, GPIO_PIN_TYPE_PULLUP);

  // GPIO_setQualificationMode(DSP_SPI_GPIO_SIMO, GPIO_QUAL_ASYNC);
  // GPIO_setQualificationMode(DSP_SPI_GPIO_CLK, GPIO_QUAL_ASYNC);
  // GPIO_setQualificationMode(DSP_SPI_GPIO_STE, GPIO_QUAL_ASYNC);

  // === SPI Config ===
  SPI_disableModule(SPIA_BASE);

  SPI_setConfig(SPIA_BASE, DEVICE_LSPCLK_FREQ, DSP_SPI_PROTO,
                SPI_MODE_SLAVE, // Slave mode
                1000000,        // Baudrate ignored in slave
                16);            // 16-bit words

  SPI_enableFIFO(SPIA_BASE);
  SPI_resetTxFIFO(SPIA_BASE);
  SPI_resetRxFIFO(SPIA_BASE);

  // === Setup RX interrupt ===
  SPI_setFIFOInterruptLevel(SPIA_BASE, SPI_FIFO_TX16, SPI_FIFO_RX1);
  SPI_clearInterruptStatus(SPIA_BASE, SPI_INT_RXFF | SPI_INT_TXFF);

  // SPI_clearInterruptStatus(SPIA_BASE, SPI_INT_RXFF);
  SPI_enableInterrupt(SPIA_BASE, SPI_INT_RXFF);
  SPI_disableInterrupt(SPIA_BASE, SPI_INT_TXFF);

  SPI_enableModule(SPIA_BASE);

  Interrupt_register(INT_SPIA_RX, &rx_fifo_isr);
  Interrupt_enable(INT_SPIA_RX);

  // === Setup CS interrupt (XINT1 on GPIO61) ===
  GPIO_setInterruptPin(DSP_SPI_GPIO_STE, GPIO_INT_XINT1);
  GPIO_setInterruptType(GPIO_INT_XINT1, GPIO_INT_TYPE_FALLING_EDGE);
  GPIO_enableInterrupt(GPIO_INT_XINT1);

  Interrupt_register(INT_XINT1, &cs_isr);
  Interrupt_enable(INT_XINT1);
}

__interrupt static void rx_fifo_isr(void) {
  static uint32_t s_rx_frameCounter = 0;       // debug variable
  static uint32_t s_max_data_read_per_isr = 0; // debug variable
  static uint32_t header_errorCounter = 0;
  uint16_t i = 0; // debug variable

  while (SPI_getRxFIFOStatus(SPIA_BASE) != SPI_FIFO_RXEMPTY) {
    i++;
    s_rx_buffer[s_rx_index++] = HWREGH(SPIA_BASE + SPI_O_RXBUF);
  }
  if (i > s_max_data_read_per_isr) {
    s_max_data_read_per_isr = i;
  }

  if (s_rx_index >= FRAME_WORDS) {
    // The rx buffer is full. Reset it.
    // s_rx_index = 0;
    // SPI_resetRxFIFO(SPIA_BASE);
    // SPI_resetTxFIFO(SPIA_BASE);
    s_rx_frameCounter++;
    for (i = 0; i < FRAME_WORDS; i++) {
      if (s_rx_buffer[i] != s_tx_buffer[i]) {
        header_errorCounter++;
      }
    }
  }

  // clear interrupt flag
  SPI_clearInterruptStatus(SPIA_BASE, SPI_INT_RXFF);
  Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP6);
}

// === CS ISR: CS goes low, a new transfer is starting ===
static bool wait_for_cs_fall = true; // false for waiting for
__interrupt static void cs_isr(void) {
  // Prepare for a new rx transfer

  if (wait_for_cs_fall) {
    static uint16_t max_wait_count = 0;
    // Prepare for a new tx transfer
    SPI_resetTxFIFO(SPIA_BASE);
    static uint16_t i = 0, j = 0;
    for (i = 0, j = 0; i < FRAME_WORDS; ++i) {
      while (SPI_getTxFIFOStatus(SPIA_BASE) == SPI_FIFO_TXFULL) {
        j++;
      }
      HWREGH(SPIA_BASE + SPI_O_TXBUF) = s_tx_buffer[i];
      if (j > max_wait_count) {
        max_wait_count = j;
      }
    }
    wait_for_cs_fall = false;
    GPIO_setInterruptType(GPIO_INT_XINT1, GPIO_INT_TYPE_RISING_EDGE);

  } else {
    // end of frame
    // reset rx buffers here
    SPI_clearRxOverflowAndReset(SPIA_BASE);
    s_rx_index = 0;

    wait_for_cs_fall = true;
    GPIO_setInterruptType(GPIO_INT_XINT1, GPIO_INT_TYPE_FALLING_EDGE);
  }

  Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

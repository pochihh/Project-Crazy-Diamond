#include "MessageCenter.h"
#include "device.h"
#include "driverlib.h"

#include "inc/hw_dma.h"    // DMA_O_DST_ADDR_SHADOW
#include "inc/hw_memmap.h" // DMA_CH1_BASE, SPIA_BASE
#include "inc/hw_types.h"  // HWREG()

#include <string.h>

// ================= Config (idle-timer knobs kept if you later switch to idle
// framing) ==
#define MC_IDLE_TIMER_BASE CPUTIMER2_BASE
#define MC_IDLE_TICK_US 100U
#define MC_IDLE_WINDOWS 3U

// ================= Internal state ===========================================
static volatile uint16_t s_txBufA[MC_TX_MAX_WORDS];
static volatile uint16_t s_txBufB[MC_TX_MAX_WORDS];
static volatile uint16_t *s_txActive = s_txBufA;
static volatile uint16_t *s_txStandby = s_txBufB;
static volatile uint16_t s_txWords = 18U; // fixed frame length

static volatile uint16_t s_rxBuf[MC_RX_MAX_WORDS];
static volatile bool s_rxOverflow = false;
static MC_RxCallback s_rxCb = 0;

static volatile bool s_txPaused = false;

#define MC_DMA_RX DMA_CH1_BASE
#define MC_DMA_TX DMA_CH2_BASE

// ================= Small helpers ============================================
static inline uint16_t lo16(uint32_t v) { return (uint16_t)(v & 0xFFFFu); }
static inline uint16_t hi16(uint32_t v) { return (uint16_t)(v >> 16); }

static inline uint16_t crc16_ccitt_step(uint16_t crc, uint16_t w) {
  int i;
  crc ^= w;
  for (i = 0; i < 16; i++)
    crc =
        (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  return crc;
}

static inline void swap_tx_buffers(void) {
  volatile uint16_t *t = s_txActive;
  s_txActive = s_txStandby;
  s_txStandby = t;
}

static inline void mc_pack_float(float f, uint16_t *lo, uint16_t *hi) {
  union {
    float f;
    uint32_t u;
  } u32 = {.f = f};
  *lo = lo16(u32.u);
  *hi = hi16(u32.u);
}

// How many RX words DMA has written so far (uses destination shadow pointer)
static inline uint16_t mc_rx_words_received(void) {
  uint32_t dst_next = HWREG(MC_DMA_RX + DMA_O_DST_ADDR_SHADOW);
  uint32_t base = (uint32_t)s_rxBuf;
  if (dst_next <= base)
    return 0;
  return (uint16_t)((dst_next - base) >> 1); // 16-bit words
}

// ================= GPIO/pinmux ==============================================
static void mc_pinmux(void) {
  // SPIA pins
  GPIO_setPinConfig(MC_SPIA_GPIO_SIMO);
  GPIO_setPinConfig(MC_SPIA_GPIO_SOMI);
  GPIO_setPinConfig(MC_SPIA_GPIO_SCLK);
  GPIO_setPinConfig(MC_SPIA_GPIO_STE);

  GPIO_setDirectionMode(MC_SPIA_GPIO_SIMO, GPIO_DIR_MODE_IN);
  GPIO_setDirectionMode(MC_SPIA_GPIO_SOMI, GPIO_DIR_MODE_OUT);
  GPIO_setDirectionMode(MC_SPIA_GPIO_SCLK, GPIO_DIR_MODE_IN);
  GPIO_setDirectionMode(MC_SPIA_GPIO_STE, GPIO_DIR_MODE_IN);

  GPIO_setPadConfig(MC_SPIA_GPIO_SIMO, GPIO_PIN_TYPE_PULLUP);
  GPIO_setPadConfig(MC_SPIA_GPIO_SCLK, GPIO_PIN_TYPE_PULLUP);
  GPIO_setPadConfig(MC_SPIA_GPIO_STE, GPIO_PIN_TYPE_PULLUP);

  // CS monitor input (wired in parallel with SPISTE if desired)
  GPIO_setDirectionMode(MC_CS_GPIO, GPIO_DIR_MODE_IN);
  GPIO_setPadConfig(MC_CS_GPIO, GPIO_PIN_TYPE_PULLUP);
  GPIO_setQualificationMode(MC_CS_GPIO, GPIO_QUAL_ASYNC);
}

// ================= SPI + DMA ================================================
__interrupt static void mc_dmaRxISR(void);
__interrupt static void mc_dmaTxISR(void);
__interrupt static void mc_csISR(void);

static void mc_cs_int_init(void) {
  // Route this GPIO to your chosen external interrupt channel (XINT1..5)
  GPIO_setInterruptPin(MC_CS_GPIO, MC_CS_XINT_CH);

  // BOTH edges; we’ll read the pin to decide rising/falling inside the ISR
  GPIO_setInterruptType(MC_CS_XINT_CH, GPIO_INT_TYPE_BOTH_EDGES);

  // Enable the external interrupt channel and hook the ISR
  GPIO_enableInterrupt(MC_CS_XINT_CH);
  Interrupt_register(MC_CS_INT_VECTOR, &mc_csISR);
  Interrupt_enable(MC_CS_INT_VECTOR);
}

static void mc_spi_init(void) {
  SPI_disableModule(SPIA_BASE);

  // Slave, 16-bit, mode0; LSPCLK param ignored in slave but required by API
  SPI_setConfig(SPIA_BASE, DEVICE_LSPCLK_FREQ, SPI_PROT_POL0PHA0,
                SPI_MODE_SLAVE, 1000000U, 16);

  SPI_enableFIFO(SPIA_BASE);
  SPI_resetRxFIFO(SPIA_BASE);
  SPI_resetTxFIFO(SPIA_BASE);
  SPI_clearInterruptStatus(SPIA_BASE, SPI_INT_RXFF | SPI_INT_TXFF);

  SPI_enableModule(SPIA_BASE);
}

static void mc_dma_init(void) {
  DMA_initController();

  // RX: fill up to MC_RX_MAX_WORDS while CS is low (flexible frame length)
  DMA_configAddresses(MC_DMA_RX, (void *)s_rxBuf,
                      (void *)(SPIA_BASE + SPI_O_RXBUF));
  DMA_configBurst(MC_DMA_RX, 1, 0, 0);
  DMA_configTransfer(MC_DMA_RX, MC_RX_MAX_WORDS, 1, 0);
  DMA_configMode(MC_DMA_RX, DMA_TRIGGER_SPIARX,
                 DMA_CFG_ONESHOT_DISABLE | DMA_CFG_CONTINUOUS_DISABLE);
  DMA_setInterruptMode(MC_DMA_RX, DMA_INT_AT_END);
  DMA_enableInterrupt(MC_DMA_RX);
  Interrupt_register(INT_DMA_CH1, &mc_dmaRxISR);
  Interrupt_enable(INT_DMA_CH1);

  // TX: pre-configured; you’re not arming TX inside CS ISR right now
  DMA_configAddresses(MC_DMA_TX, (void *)s_txActive,
                      (void *)(SPIA_BASE + SPI_O_TXBUF));
  DMA_configBurst(MC_DMA_TX, 1, 0, 0);
  DMA_configTransfer(MC_DMA_TX, s_txWords, 1, 0);
  DMA_configMode(MC_DMA_TX, DMA_TRIGGER_SPIATX,
                 DMA_CFG_ONESHOT_DISABLE | DMA_CFG_CONTINUOUS_DISABLE);
  DMA_setInterruptMode(MC_DMA_TX, DMA_INT_AT_END);
  DMA_enableInterrupt(MC_DMA_TX);
  Interrupt_register(INT_DMA_CH2, &mc_dmaTxISR);
  Interrupt_enable(INT_DMA_CH2);

  // NOTE: CS ISR registration is done in mc_cs_int_init(), not here.
}

// ================= Public API ===============================================
void MessageCenter_init(void) {
  uint16_t i;
  mc_pinmux();
  mc_spi_init();
  mc_dma_init();
  mc_cs_int_init();

  // Build a known first frame (zeros) into standby, then swap
  s_txStandby[0] = 0xA5A5; // magic
  s_txStandby[1] = 0x0002; // version
  s_txStandby[2] = 14U;    // payload words [3..16]
  for (i = 3; i <= 16; ++i)
    s_txStandby[i] = 0U;

  uint16_t crc = 0xFFFF;
  for (i = 0; i <= 16; ++i)
    crc = crc16_ccitt_step(crc, s_txStandby[i]);
  s_txStandby[17] = crc;

  swap_tx_buffers();
}

void MessageCenter_setRxCallback(MC_RxCallback cb) { s_rxCb = cb; }

void MessageCenter_tick(uint32_t ts_us, int32_t pos1, float speed1_cps,
                        uint32_t err1, int32_t pos2, float speed2_cps,
                        uint32_t err2) {
  volatile uint16_t *out = s_txStandby;

  out[0] = 0xA5A5;
  out[1] = 0x0002;
  out[2] = 14U;

  // timestamp
  out[3] = lo16(ts_us);
  out[4] = hi16(ts_us);

  // CH1
  out[5] = (uint16_t)((uint32_t)pos1 & 0xFFFFu);
  out[6] = (uint16_t)(((uint32_t)pos1) >> 16);
  mc_pack_float(speed1_cps, (uint16_t *)&out[7], (uint16_t *)&out[8]);
  out[9] = lo16(err1);
  out[10] = hi16(err1);

  // CH2
  out[11] = (uint16_t)((uint32_t)pos2 & 0xFFFFu);
  out[12] = (uint16_t)(((uint32_t)pos2) >> 16);
  mc_pack_float(speed2_cps, (uint16_t *)&out[13], (uint16_t *)&out[14]);
  out[15] = lo16(err2);
  out[16] = hi16(err2);

  // CRC over [0..16]
  uint16_t crc = 0xFFFF;
  uint16_t i;
  for (i = 0; i <= 16; ++i)
    crc = crc16_ccitt_step(crc, out[i]);
  out[17] = crc;

  swap_tx_buffers();
}

void MessageCenter_pauseTx(bool pause) { s_txPaused = pause; }

// ================= ISRs ======================================================
// CS-framed, flexible-length RX.
// We configured BOTH edges on MC_CS_XINT_CH; read the pin to know which edge.
__interrupt static void mc_csISR(void) {
  static bool awaiting_end = false;
  const uint16_t cs_now = GPIO_readPin(MC_CS_GPIO);

  if (!awaiting_end && cs_now == 0U) {
    // -------- CS FALLING: START FRAME --------
    awaiting_end = true;
    s_rxOverflow = false;

    // Clean FIFOs
    SPI_resetRxFIFO(SPIA_BASE);
    SPI_resetTxFIFO(SPIA_BASE);
    SPI_clearInterruptStatus(SPIA_BASE, SPI_INT_RXFF | SPI_INT_TXFF);

    // RX: arm DMA to capture any command bytes (flexible length)
    DMA_configAddresses(MC_DMA_RX, (void *)s_rxBuf,
                        (void *)(SPIA_BASE + SPI_O_RXBUF));
    DMA_configTransfer(MC_DMA_RX, MC_RX_MAX_WORDS, 1, 0);
    DMA_startChannel(MC_DMA_RX);

    // TX: stream current frame in s_txActive
    if (!s_txPaused) {
      DMA_configAddresses(MC_DMA_TX, (void *)s_txActive,
                          (void *)(SPIA_BASE + SPI_O_TXBUF));
      DMA_configTransfer(MC_DMA_TX, s_txWords, 1, 0);
      DMA_startChannel(MC_DMA_TX);
    }
  } else if (awaiting_end && cs_now == 1U) {
    // -------- CS RISING: END FRAME --------
    awaiting_end = false;

    // Stop DMAs (ok if already done)
    DMA_stopChannel(MC_DMA_RX);
    DMA_stopChannel(MC_DMA_TX);

    const uint16_t got = mc_rx_words_received();

    // Housekeeping
    SPI_resetRxFIFO(SPIA_BASE);
    SPI_resetTxFIFO(SPIA_BASE);
    SPI_clearInterruptStatus(SPIA_BASE, SPI_INT_RXFF | SPI_INT_TXFF);

    if (s_rxCb && got > 0U)
      s_rxCb((const uint16_t *)s_rxBuf, got, s_rxOverflow);
  }

  Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

// RX buffer filled before CS↑ → overflow; frame will still finalize on CS
// rising
__interrupt static void mc_dmaRxISR(void) {
  DMA_clearTriggerFlag(MC_DMA_RX);
  s_rxOverflow = true;
  Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP7);
}

__interrupt static void mc_dmaTxISR(void) {
  DMA_clearTriggerFlag(MC_DMA_TX);
  Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP7);
}

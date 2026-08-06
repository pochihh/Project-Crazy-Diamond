//#############################################################################
//
// FILE:   spi_dma.c
//
// TITLE:  DMA-driven SPI slave for F28379D — production driver
//
// DESCRIPTION:
//   Full-duplex SPI slave using 2 DMA channels (RX CH1, TX CH2) with CS
//   interrupt for frame boundary detection. No test code, no main().
//   Include this file in the main application build.
//
//   Architecture:
//     - DMA CH1 (trigger 110): SPI RX FIFO -> g_rxActive  (one word per trigger)
//     - DMA CH2 (trigger 109): g_txActive  -> SPI TX FIFO (one word per trigger)
//     - XINT5 on GPIO40 (CS mirror): both edges, guarded by g_cs_active
//     - Double-buffered TX and RX; CPU calls SpiDma_updateTx() from main loop
//
//   CS edge debounce:
//     g_cs_active prevents processing the same frame twice if the XINT5
//     fires spuriously (e.g., GPIO glitch or PIE re-pending). This is the
//     root cause of the "exactly half fail" bug seen in Phase C testing.
//
//#############################################################################

#include "spi_dma.h"
#include "driverlib.h"
#include "device.h"
#include <string.h>

//
// ============ DMA Buffers (placed in .dma_buffers section) ============
//
#pragma DATA_SECTION(g_rxBufferA, ".dma_buffers")
#pragma DATA_SECTION(g_rxBufferB, ".dma_buffers")
#pragma DATA_SECTION(g_txBufferA, ".dma_buffers")
#pragma DATA_SECTION(g_txBufferB, ".dma_buffers")

static uint16_t g_rxBufferA[MAX_FRAME_WORDS];
static uint16_t g_rxBufferB[MAX_FRAME_WORDS];
static uint16_t g_txBufferA[MAX_FRAME_WORDS];
static uint16_t g_txBufferB[MAX_FRAME_WORDS];

static uint16_t *g_rxActive  = g_rxBufferA;
static uint16_t *g_rxDone    = g_rxBufferB;
static uint16_t *g_txActive  = g_txBufferA;
static uint16_t *g_txStandby = g_txBufferB;

static volatile bool g_txDataReady = false;

// CS state: true while CS is asserted (transfer in progress)
static volatile bool g_cs_active = false;

static volatile SpiDmaStats_t g_stats = {0};
static SpiDma_rxCallback g_rxCallback = NULL;

//
// ============ Forward Declarations ============
//
__interrupt void SpiDma_csIsr(void);
static void configSpiPins(void);
static void configSpi(void);
static void configDmaChannels(void);
static void configCsInterrupt(void);
static void startDma(void);
static void stopDma(void);
static void rearmDma(void);
static void swapRxBuffers(void);
static void swapTxBuffers(void);
static bool validateAndParseRx(const uint16_t *buf);

static inline uint16_t lo16(uint32_t v) { return (uint16_t)(v & 0xFFFFu); }
static inline uint16_t hi16(uint32_t v) { return (uint16_t)(v >> 16); }

static inline void pack_float(float f, uint16_t *lo, uint16_t *hi) {
    union { float f; uint32_t u; } c; c.f = f;
    *lo = (uint16_t)(c.u & 0xFFFFu);
    *hi = (uint16_t)(c.u >> 16);
}

static inline float unpack_float(uint16_t lo_w, uint16_t hi_w) {
    union { float f; uint32_t u; } c;
    c.u = ((uint32_t)hi_w << 16) | lo_w;
    return c.f;
}

//
// ============ ISR runs from RAM for deterministic latency ============
//
#pragma CODE_SECTION(SpiDma_csIsr, ".TI.ramfunc")

//
// ============ CRC-16/CCITT-FALSE ============
//
// poly=0x1021, init=0xFFFF, RefIn=false, RefOut=false, XorOut=0
// Each 16-bit word is fed high byte first, matching SPI bit order.
// Note: on C2000 uint8_t is 16-bit, so bytes are extracted manually.
//
static uint16_t calcCRC16(const uint16_t *buf, uint16_t n_words)
{
    uint16_t crc = 0xFFFF;
    uint16_t i, j, b;
    for (i = 0; i < n_words; i++) {
        b = (buf[i] >> 8) & 0xFF;           // high byte first
        crc ^= (b << 8);
        for (j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        b = buf[i] & 0xFF;                  // then low byte
        crc ^= (b << 8);
        for (j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

//
// ============ SPI Pin Configuration ============
//
static void configSpiPins(void)
{
    GPIO_setPinConfig(GPIO_58_SPISIMOA);
    GPIO_setPinConfig(GPIO_59_SPISOMIA);
    GPIO_setPinConfig(GPIO_60_SPICLKA);
    GPIO_setPinConfig(GPIO_61_SPISTEA);

    GPIO_setDirectionMode(DSP_SPI_GPIO_SIMO, GPIO_DIR_MODE_IN);
    GPIO_setDirectionMode(DSP_SPI_GPIO_SOMI, GPIO_DIR_MODE_OUT);
    GPIO_setDirectionMode(DSP_SPI_GPIO_CLK,  GPIO_DIR_MODE_IN);
    GPIO_setDirectionMode(DSP_SPI_GPIO_STE,  GPIO_DIR_MODE_IN);

    GPIO_setPadConfig(DSP_SPI_GPIO_CLK, GPIO_PIN_TYPE_PULLUP);
    GPIO_setPadConfig(DSP_SPI_GPIO_STE, GPIO_PIN_TYPE_PULLUP);

    // SPI hardware sees unfiltered CLK/STE; CS mirror filtered for XINT
    GPIO_setQualificationMode(DSP_SPI_GPIO_CLK, GPIO_QUAL_ASYNC);
    GPIO_setQualificationMode(DSP_SPI_GPIO_STE, GPIO_QUAL_ASYNC);

    // CS mirror: 6-sample (30 ns at 200 MHz) filters glitch-induced double IRQs
    GPIO_setPinConfig(GPIO_40_GPIO40);
    GPIO_setDirectionMode(DSP_SPI_GPIO_CS_MIRROR, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(DSP_SPI_GPIO_CS_MIRROR, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(DSP_SPI_GPIO_CS_MIRROR, GPIO_QUAL_6SAMPLE);

    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED1, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED1, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED2, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED2, GPIO_DIR_MODE_OUT);

    GPIO_writePin(DEVICE_GPIO_PIN_LED1, 1);  // off (active low)
    GPIO_writePin(DEVICE_GPIO_PIN_LED2, 1);
}

//
// ============ SPI Module Configuration ============
//
static void configSpi(void)
{
    SPI_disableModule(SPIA_BASE);

    SPI_setConfig(SPIA_BASE, DEVICE_LSPCLK_FREQ,
                  SPI_PROT_POL1PHA1,
                  SPI_MODE_SLAVE,
                  1000000,   // ignored in slave mode
                  16);

    SPI_setEmulationMode(SPIA_BASE, SPI_EMULATION_FREE_RUN);
    SPI_enableFIFO(SPIA_BASE);
    SPI_resetTxFIFO(SPIA_BASE);
    SPI_resetRxFIFO(SPIA_BASE);
    SPI_setFIFOInterruptLevel(SPIA_BASE, SPI_FIFO_TX1, SPI_FIFO_RX1);
    SPI_enableTalk(SPIA_BASE);
    SPI_disableInterrupt(SPIA_BASE, SPI_INT_RXFF | SPI_INT_TXFF);
    SPI_disableLoopback(SPIA_BASE);
    SPI_enableModule(SPIA_BASE);
}

//
// ============ DMA Channel Configuration ============
//
static void configDmaChannels(void)
{
    DMA_initController();
    DMA_setEmulationMode(DMA_EMULATION_FREE_RUN);

    // Connect DMA to Peripheral Frame 2 (SPI/SCI) bridge
    SysCtl_selectSecController(SYSCTL_SEC_CONTROLLER_CLA,
                               SYSCTL_SEC_CONTROLLER_DMA);

    // RX: SPI RX FIFO (fixed) -> g_rxActive (incrementing)
    DMA_configAddresses(SPI_DMA_RX_CH,
                        (const void *)g_rxActive,
                        (const void *)(SPIA_BASE + SPI_O_RXBUF));
    DMA_configBurst(SPI_DMA_RX_CH, 1, 0, 1);
    DMA_configTransfer(SPI_DMA_RX_CH, MAX_FRAME_WORDS, 0, 1);
    DMA_configWrap(SPI_DMA_RX_CH, 0xFFFF, 0, 0xFFFF, 0);
    DMA_configMode(SPI_DMA_RX_CH, (DMA_Trigger)SPI_DMA_RX_TRIGGER,
                   DMA_CFG_ONESHOT_DISABLE | DMA_CFG_CONTINUOUS_DISABLE |
                   DMA_CFG_SIZE_16BIT);
    DMA_enableTrigger(SPI_DMA_RX_CH);

    // TX: g_txActive (incrementing) -> SPI TX FIFO (fixed)
    DMA_configAddresses(SPI_DMA_TX_CH,
                        (const void *)(SPIA_BASE + SPI_O_TXBUF),
                        (const void *)g_txActive);
    DMA_configBurst(SPI_DMA_TX_CH, 1, 1, 0);
    DMA_configTransfer(SPI_DMA_TX_CH, MAX_FRAME_WORDS, 1, 0);
    DMA_configWrap(SPI_DMA_TX_CH, 0xFFFF, 0, 0xFFFF, 0);
    DMA_configMode(SPI_DMA_TX_CH, (DMA_Trigger)SPI_DMA_TX_TRIGGER,
                   DMA_CFG_ONESHOT_DISABLE | DMA_CFG_CONTINUOUS_DISABLE |
                   DMA_CFG_SIZE_16BIT);
    DMA_enableTrigger(SPI_DMA_TX_CH);
}

//
// ============ CS Interrupt Configuration ============
//
static void configCsInterrupt(void)
{
    GPIO_setInterruptPin(DSP_SPI_GPIO_CS_MIRROR, GPIO_INT_XINT5);
    GPIO_setInterruptType(GPIO_INT_XINT5, GPIO_INT_TYPE_BOTH_EDGES);
    GPIO_enableInterrupt(GPIO_INT_XINT5);

    Interrupt_register(INT_XINT5, &SpiDma_csIsr);
    Interrupt_enable(INT_XINT5);
}

//
// ============ DMA Start/Stop/Rearm ============
//
static void startDma(void)
{
    DMA_startChannel(SPI_DMA_RX_CH);
    DMA_startChannel(SPI_DMA_TX_CH);
}

static void stopDma(void)
{
    DMA_stopChannel(SPI_DMA_RX_CH);
    DMA_stopChannel(SPI_DMA_TX_CH);
}

static void rearmDma(void)
{
    DMA_configAddresses(SPI_DMA_RX_CH,
                        (const void *)g_rxActive,
                        (const void *)(SPIA_BASE + SPI_O_RXBUF));
    DMA_configAddresses(SPI_DMA_TX_CH,
                        (const void *)(SPIA_BASE + SPI_O_TXBUF),
                        (const void *)g_txActive);

    SPI_resetTxFIFO(SPIA_BASE);
    SPI_resetRxFIFO(SPIA_BASE);

    DMA_enableTrigger(SPI_DMA_RX_CH);
    DMA_enableTrigger(SPI_DMA_TX_CH);
    startDma();
}

//
// ============ Buffer Swap Helpers ============
//
static void swapRxBuffers(void)
{
    uint16_t *tmp = g_rxActive;
    g_rxActive    = g_rxDone;
    g_rxDone      = tmp;
}

static void swapTxBuffers(void)
{
    if (g_txDataReady) {
        uint16_t *tmp = g_txActive;
        g_txActive    = g_txStandby;
        g_txStandby   = tmp;
        g_txDataReady = false;
    }
}

//
// ============ RX Frame Validation and Parsing ============
//
// Validates the RPi -> DSP frame (RX format: 0x55AA header, 17 words).
// On success, extracts payload and invokes the registered callback.
//
static bool validateAndParseRx(const uint16_t *buf)
{
    if (buf[0] != RX_FRAME_HEADER) {
        g_stats.frame_errors++;
        return false;
    }
    if (buf[1] != RX_FRAME_VERSION) {
        g_stats.frame_errors++;
        return false;
    }

    uint16_t crc = calcCRC16(buf, RX_FRAME_WORDS - 1);
    if (crc != buf[RX_FRAME_WORDS - 1]) {
        g_stats.crc_errors++;
        return false;
    }

    if (g_rxCallback) {
        RxFrame_t rx;
        uint16_t p = 2;
        rx.cmd  = ((uint32_t)buf[p + 1] << 16) | buf[p]; p += 2;
        rx.ref[0] = unpack_float(buf[p], buf[p+1]); p += 2;
        rx.ref[1] = unpack_float(buf[p], buf[p+1]); p += 2;
        rx.ref[2] = unpack_float(buf[p], buf[p+1]); p += 2;
        rx.ref[3] = unpack_float(buf[p], buf[p+1]); p += 2;
        rx.ref[4] = unpack_float(buf[p], buf[p+1]); p += 2;
        rx.ref[5] = unpack_float(buf[p], buf[p+1]);
        g_rxCallback(&rx);
    }

    return true;
}

//
// ============ CS ISR ============
//
__interrupt void SpiDma_csIsr(void)
{
    if (GPIO_readPin(DSP_SPI_GPIO_CS_MIRROR) == 0) {
        // CS falling — transfer starting
        if (!g_cs_active) {
            g_cs_active = true;
            g_stats.cs_falling_count++;
        } else {
            g_stats.cs_edge_ignored++;
        }
    }
    else {
        // CS rising — transfer complete
        if (g_cs_active) {
            g_cs_active = false;
            g_stats.cs_rising_count++;

            stopDma();
            swapRxBuffers();

            if (validateAndParseRx(g_rxDone)) {
                g_stats.rx_frame_count++;
                GPIO_togglePin(DEVICE_GPIO_PIN_LED1);
            } else {
                GPIO_writePin(DEVICE_GPIO_PIN_LED2, 0);
            }

            g_stats.tx_frame_count++;
            swapTxBuffers();
            rearmDma();
        } else {
            g_stats.cs_edge_ignored++;
        }
    }

    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP12);
}

//
// ============ Public API ============
//

void SpiDma_init(void)
{
    memset(g_rxBufferA, 0, sizeof(g_rxBufferA));
    memset(g_rxBufferB, 0, sizeof(g_rxBufferB));
    memset(g_txBufferA, 0, sizeof(g_txBufferA));
    memset(g_txBufferB, 0, sizeof(g_txBufferB));
    memset((void *)&g_stats, 0, sizeof(g_stats));

    g_cs_active   = false;
    g_txDataReady = false;
    g_rxCallback  = NULL;

    configSpiPins();
    configSpi();
    configDmaChannels();
    configCsInterrupt();

    // Clear FIFO state before first frame
    SPI_resetTxFIFO(SPIA_BASE);
    SPI_resetRxFIFO(SPIA_BASE);

    startDma();
}

void SpiDma_updateTx(const TxFrame_t *data)
{
    if (!data) return;

    uint16_t *buf = g_txStandby;
    uint16_t i, k;

    for (i = 0; i < MAX_FRAME_WORDS; i++) buf[i] = 0;
    for (i = 0; i < TX_FRAME_NUM_DUMMY; i++) buf[i] = TX_FRAME_DUMMY_WORD;

    i = TX_FRAME_NUM_DUMMY;
    buf[i++] = TX_FRAME_HEADER;
    buf[i++] = TX_FRAME_VERSION;

    buf[i++] = lo16(data->timestamp_us);
    buf[i++] = hi16(data->timestamp_us);

    for (k = 0; k < 6; k++) { pack_float(data->ref[k], &buf[i], &buf[i+1]); i += 2; }
    for (k = 0; k < 6; k++) { buf[i++] = lo16((uint32_t)data->pos[k]); buf[i++] = hi16((uint32_t)data->pos[k]); }
    for (k = 0; k < 6; k++) { pack_float(data->u[k], &buf[i], &buf[i+1]); i += 2; }

    buf[i++] = data->err_bitmap;
    buf[i++] = data->err_count;

    for (k = 0; k < 6; k++) buf[i++] = data->adc[k];
    for (k = 0; k < 4; k++) { pack_float(data->quat[k], &buf[i], &buf[i+1]); i += 2; }

    buf[i] = calcCRC16(&buf[TX_FRAME_NUM_DUMMY], TX_FRAME_DATA_WORDS - 1);

    g_txDataReady = true;
}

void SpiDma_setRxCallback(SpiDma_rxCallback fn)
{
    g_rxCallback = fn;
}

void SpiDma_getStats(SpiDmaStats_t *out)
{
    if (out) *out = g_stats;
}

volatile SpiDmaStats_t* SpiDma_getStatsPtr(void)
{
    return &g_stats;
}

//
// End of File
//

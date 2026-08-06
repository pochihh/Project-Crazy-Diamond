//#############################################################################
//
// FILE:   spi_dma_test.c
//
// TITLE:  DMA-driven SPI slave for F28379D — test harness
//
// DESCRIPTION:
//   Full-duplex SPI slave using 2 DMA channels (RX + TX) with CS interrupt
//   for frame boundary detection. Test harness for Phase A/B/C validation.
//
//   Test modes:
//     A) Internal loopback (no wiring, DSP master)
//     B) External loopback (MOSI->MISO jumper, RPi provides CLK+CS)
//     C) RPi communication (full system test)
//
//   LED indicators:
//     LED1: blinks on successful frame
//     LED2: solid on error
//
//   CS edge debounce:
//     g_cs_active tracks whether CS is currently low (transfer in progress).
//     Spurious/double-trigger interrupts are suppressed by checking this flag
//     before processing. This fixes the "exactly half fail" Phase C bug where
//     the ISR could fire twice for one rising edge, swapping to an empty buffer.
//
//#############################################################################

#include "spi_dma_test.h"
#include "driverlib.h"
#include "device.h"
#include <string.h>

//
// ============ Build Configuration ============
//
// Set test mode here (or override via CCS predefined symbols)
#ifndef SPI_TEST_MODE
#define SPI_TEST_MODE   TEST_MODE_RPI_COMM
#endif

//
// ============ DMA Buffers (placed in .dma_buffers via linker) ============
//
#pragma DATA_SECTION(g_rxBufferA, ".dma_buffers")
#pragma DATA_SECTION(g_rxBufferB, ".dma_buffers")
#pragma DATA_SECTION(g_txBufferA, ".dma_buffers")
#pragma DATA_SECTION(g_txBufferB, ".dma_buffers")

static uint16_t g_rxBufferA[MAX_FRAME_WORDS];
static uint16_t g_rxBufferB[MAX_FRAME_WORDS];
static uint16_t g_txBufferA[MAX_FRAME_WORDS];
static uint16_t g_txBufferB[MAX_FRAME_WORDS];

// Double-buffer pointers
static uint16_t *g_rxActive  = g_rxBufferA;    // DMA writes here
static uint16_t *g_rxDone    = g_rxBufferB;    // CPU reads from here
static uint16_t *g_txActive  = g_txBufferA;    // DMA reads from here
static uint16_t *g_txStandby = g_txBufferB;    // CPU writes here

static volatile bool g_txDataReady = false;

//
// CS edge state: true = CS is currently low (transfer in progress).
// Used to suppress spurious double-trigger interrupts.
//
static volatile bool g_cs_active = false;

// Statistics
static volatile SpiDmaStats_t g_stats = {0};

// Callback
static SpiDma_rxCallback g_rxCallback = NULL;

// Current test mode
static TestMode_t g_testMode = TEST_MODE_INTERNAL_LOOPBACK;

// Test pattern counter (for loopback verification)
static volatile uint16_t g_testPatternCounter = 0;
static volatile uint32_t g_loopbackErrors = 0;
static volatile uint32_t g_loopbackOk = 0;

//
// ============ Forward Declarations ============
//
__interrupt void cs_isr(void);
static void configSpiPins(void);
static void configSpi(void);
static void configDmaChannels(void);
static void configCsInterrupt(void);
static void startDma(void);
static void stopDma(void);
static void rearmDma(void);
static void swapRxBuffers(void);
static void swapTxBuffers(void);

static uint16_t calcCRC16(const uint16_t *w, uint16_t size);
static void buildTestTxFrame(uint16_t *buf, uint16_t pattern);
static bool validateRxFrame(const uint16_t *buf);
static void verifyLoopback(const uint16_t *rx, const uint16_t *tx);

static inline uint16_t lo16(uint32_t v) { return (uint16_t)(v & 0xFFFFu); }
static inline uint16_t hi16(uint32_t v) { return (uint16_t)(v >> 16); }

static inline void pack_float(float f, volatile uint16_t *lo, volatile uint16_t *hi) {
    union { float f; uint32_t u; } conv;
    conv.f = f;
    *lo = (uint16_t)(conv.u & 0xFFFFu);
    *hi = (uint16_t)(conv.u >> 16);
}

static inline float unpack_float(uint16_t lo_w, uint16_t hi_w) {
    union { float f; uint32_t u; } conv;
    conv.u = ((uint32_t)hi_w << 16) | lo_w;
    return conv.f;
}

//
// ============ Ramfunc declarations (run from RAM for speed) ============
//
#pragma CODE_SECTION(cs_isr, ".TI.ramfunc")

//
// ============ CRC-16/CCITT-FALSE ============
//
// poly=0x1021, init=0xFFFF, RefIn=false, RefOut=false, XorOut=0
// Each 16-bit word fed high byte first (MSB-first), matching SPI bit order.
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
    // SPIA pins: GPIO 58(SIMO), 59(SOMI), 60(CLK), 61(STE)
    GPIO_setPinConfig(GPIO_58_SPISIMOA);
    GPIO_setPinConfig(GPIO_59_SPISOMIA);
    GPIO_setPinConfig(GPIO_60_SPICLKA);
    GPIO_setPinConfig(GPIO_61_SPISTEA);

    // Slave mode directions: SIMO=in, SOMI=out
    GPIO_setDirectionMode(DSP_SPI_GPIO_SIMO, GPIO_DIR_MODE_IN);
    GPIO_setDirectionMode(DSP_SPI_GPIO_SOMI, GPIO_DIR_MODE_OUT);
    GPIO_setDirectionMode(DSP_SPI_GPIO_CLK,  GPIO_DIR_MODE_IN);
    GPIO_setDirectionMode(DSP_SPI_GPIO_STE,  GPIO_DIR_MODE_IN);

    // Pull-ups on clock and chip-select for clean idle state
    GPIO_setPadConfig(DSP_SPI_GPIO_CLK, GPIO_PIN_TYPE_PULLUP);
    GPIO_setPadConfig(DSP_SPI_GPIO_STE, GPIO_PIN_TYPE_PULLUP);

    // Async qualification on CLK/STE so SPI hardware sees the signal unfiltered
    GPIO_setQualificationMode(DSP_SPI_GPIO_CLK, GPIO_QUAL_ASYNC);
    GPIO_setQualificationMode(DSP_SPI_GPIO_STE, GPIO_QUAL_ASYNC);

    // CS mirror pin (GPIO40) for XINT5 interrupt
    // Use 6-sample qualification (30ns at 200MHz) to suppress glitch-triggered
    // double interrupts that caused exactly-half-fail in Phase C.
    GPIO_setPinConfig(GPIO_40_GPIO40);
    GPIO_setDirectionMode(DSP_SPI_GPIO_CS_MIRROR, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(DSP_SPI_GPIO_CS_MIRROR, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(DSP_SPI_GPIO_CS_MIRROR, GPIO_QUAL_6SAMPLE);

    // LEDs
    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED1, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED1, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED2, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED2, GPIO_DIR_MODE_OUT);

    // LEDs off (active low on LaunchPad)
    GPIO_writePin(DEVICE_GPIO_PIN_LED1, 1);
    GPIO_writePin(DEVICE_GPIO_PIN_LED2, 1);
}

//
// ============ SPI Module Configuration ============
//
static void configSpi(void)
{
    SPI_disableModule(SPIA_BASE);

    SPI_setConfig(SPIA_BASE, DEVICE_LSPCLK_FREQ,
                  SPI_PROT_POL1PHA1,    // Mode 3 (matching old project)
                  SPI_MODE_SLAVE,
                  1000000,              // Baud rate ignored in slave mode
                  16);                  // 16-bit words

    SPI_setEmulationMode(SPIA_BASE, SPI_EMULATION_FREE_RUN);

    // Enable FIFO
    SPI_enableFIFO(SPIA_BASE);
    SPI_resetTxFIFO(SPIA_BASE);
    SPI_resetRxFIFO(SPIA_BASE);

    // FIFO interrupt levels: trigger DMA on each word
    SPI_setFIFOInterruptLevel(SPIA_BASE, SPI_FIFO_TX1, SPI_FIFO_RX1);

    // Enable talk (allow TX output)
    SPI_enableTalk(SPIA_BASE);

    // Disable CPU SPI interrupts — DMA handles data transfer
    SPI_disableInterrupt(SPIA_BASE, SPI_INT_RXFF | SPI_INT_TXFF);

    // Enable loopback only for internal loopback test
    if (g_testMode == TEST_MODE_INTERNAL_LOOPBACK) {
        SPI_enableLoopback(SPIA_BASE);
    } else {
        SPI_disableLoopback(SPIA_BASE);
    }

    SPI_enableModule(SPIA_BASE);
}

//
// ============ DMA Channel Configuration ============
//
static void configDmaChannels(void)
{
    DMA_initController();
    DMA_setEmulationMode(DMA_EMULATION_FREE_RUN);

    // CRITICAL: Connect DMA to Peripheral Frame 2 (PF2) bridge.
    // Without this the DMA cannot access SPI FIFO registers.
    SysCtl_selectSecController(SYSCTL_SEC_CONTROLLER_CLA,
                               SYSCTL_SEC_CONTROLLER_DMA);

    //
    // --- RX Channel (CH1): SPI RX FIFO -> RAM buffer ---
    //
    DMA_configAddresses(SPI_DMA_RX_CH,
                        (const void *)g_rxActive,
                        (const void *)(SPIA_BASE + SPI_O_RXBUF));

    DMA_configBurst(SPI_DMA_RX_CH,
                    1,      // burst size = 1 word
                    0,      // src step = 0 (FIFO fixed address)
                    1);     // dest step = 1 (increment through buffer)

    DMA_configTransfer(SPI_DMA_RX_CH,
                       MAX_FRAME_WORDS,
                       0,
                       1);

    DMA_configWrap(SPI_DMA_RX_CH, 0xFFFF, 0, 0xFFFF, 0);

    DMA_configMode(SPI_DMA_RX_CH,
                   SPI_DMA_RX_TRIGGER,
                   DMA_CFG_ONESHOT_DISABLE |
                   DMA_CFG_CONTINUOUS_DISABLE |
                   DMA_CFG_SIZE_16BIT);

    DMA_enableTrigger(SPI_DMA_RX_CH);

    //
    // --- TX Channel (CH2): RAM buffer -> SPI TX FIFO ---
    //
    DMA_configAddresses(SPI_DMA_TX_CH,
                        (const void *)(SPIA_BASE + SPI_O_TXBUF),
                        (const void *)g_txActive);

    DMA_configBurst(SPI_DMA_TX_CH,
                    1,      // burst size = 1 word
                    1,      // src step = 1 (increment through buffer)
                    0);     // dest step = 0 (FIFO fixed address)

    DMA_configTransfer(SPI_DMA_TX_CH,
                       MAX_FRAME_WORDS,
                       1,
                       0);

    DMA_configWrap(SPI_DMA_TX_CH, 0xFFFF, 0, 0xFFFF, 0);

    DMA_configMode(SPI_DMA_TX_CH,
                   SPI_DMA_TX_TRIGGER,
                   DMA_CFG_ONESHOT_DISABLE |
                   DMA_CFG_CONTINUOUS_DISABLE |
                   DMA_CFG_SIZE_16BIT);

    DMA_enableTrigger(SPI_DMA_TX_CH);
}

//
// ============ CS Interrupt Configuration ============
//
static void configCsInterrupt(void)
{
    if (g_testMode == TEST_MODE_INTERNAL_LOOPBACK) {
        return;
    }

    GPIO_setInterruptPin(DSP_SPI_GPIO_CS_MIRROR, GPIO_INT_XINT5);
    GPIO_setInterruptType(GPIO_INT_XINT5, GPIO_INT_TYPE_BOTH_EDGES);
    GPIO_enableInterrupt(GPIO_INT_XINT5);

    Interrupt_register(INT_XINT5, &cs_isr);
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
    g_rxActive = g_rxDone;
    g_rxDone = tmp;
}

static void swapTxBuffers(void)
{
    if (g_txDataReady) {
        uint16_t *tmp = g_txActive;
        g_txActive = g_txStandby;
        g_txStandby = tmp;
        g_txDataReady = false;
    }
}

//
// ============ Build Test TX Frame ============
//
// Pattern number is stored in timestamp_us for RPi-side sequence verification.
// pos[0]=pattern, pos[1]=~pattern for easy sanity checking.
// adc[0..5]=pattern+0..5, quat={1,0,0,0} (identity quaternion).
//
static void buildTestTxFrame(uint16_t *buf, uint16_t pattern)
{
    uint16_t i, k;

    for (i = 0; i < MAX_FRAME_WORDS; i++) buf[i] = 0;
    for (i = 0; i < TX_FRAME_NUM_DUMMY; i++) buf[i] = TX_FRAME_DUMMY_WORD;

    i = TX_FRAME_NUM_DUMMY;
    buf[i++] = TX_FRAME_HEADER;
    buf[i++] = TX_FRAME_VERSION;

    // timestamp_us = sequence number (lo16 only; hi16 = 0)
    buf[i++] = pattern;
    buf[i++] = 0;

    // ref[6] = 0
    for (k = 0; k < 6; k++) { buf[i++] = 0; buf[i++] = 0; }

    // pos[0]=pattern, pos[1]=~pattern, pos[2..5]=0
    buf[i++] = pattern; buf[i++] = 0;
    buf[i++] = (uint16_t)(~pattern); buf[i++] = 0xFFFF;
    for (k = 2; k < 6; k++) { buf[i++] = 0; buf[i++] = 0; }

    // u[6] = 0
    for (k = 0; k < 6; k++) { buf[i++] = 0; buf[i++] = 0; }

    // err_bitmap=0, err_count=0
    buf[i++] = 0;
    buf[i++] = 0;

    // adc[k] = pattern+k
    for (k = 0; k < 6; k++) buf[i++] = (uint16_t)(pattern + k);

    // quat = {1.0f, 0, 0, 0} — identity quaternion
    // 1.0f packed: 0x3F80 hi, 0x0000 lo
    buf[i++] = 0x0000;  // quat_w lo
    buf[i++] = 0x3F80;  // quat_w hi
    for (k = 1; k < 4; k++) { buf[i++] = 0; buf[i++] = 0; }

    buf[i] = calcCRC16(&buf[TX_FRAME_NUM_DUMMY], TX_FRAME_DATA_WORDS - 1);
}

//
// ============ Validate RX Frame ============
//
// Phase A/B (loopback): received data is the DSP's own TX frame.
//   Header = 0xAA55, CRC over TX_FRAME_DATA_WORDS-1 words.
//
// Phase C (RPi comm): received data is the RPi's RX frame.
//   Header = 0x55AA, CRC over RX_FRAME_WORDS-1 = 8 words.
//
static bool validateRxFrame(const uint16_t *buf)
{
    if (g_testMode == TEST_MODE_RPI_COMM) {
        //
        // Phase C: validate RPi -> DSP frame (RX frame format)
        //
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
        return true;
    }
    else {
        //
        // Phase A/B: loopback — received data is DSP's own TX frame.
        // Search for TX_FRAME_HEADER, then validate CRC over TX_FRAME_DATA_WORDS-1=56 words.
        //
        uint16_t i;
        for (i = 0; i < MAX_FRAME_WORDS; i++) {
            if (buf[i] == TX_FRAME_HEADER) break;
        }
        if (i >= MAX_FRAME_WORDS || (i + TX_FRAME_DATA_WORDS) > MAX_FRAME_WORDS) {
            g_stats.frame_errors++;
            return false;
        }
        if (buf[i + 1] != TX_FRAME_VERSION) {
            g_stats.frame_errors++;
            return false;
        }
        uint16_t crc = calcCRC16(&buf[i], TX_FRAME_DATA_WORDS - 1);
        if (crc != buf[i + TX_FRAME_DATA_WORDS - 1]) {
            g_stats.crc_errors++;
            return false;
        }
        return true;
    }
}

//
// ============ Loopback Verification ============
//
static void verifyLoopback(const uint16_t *rx, const uint16_t *tx)
{
    uint16_t i;
    bool match = true;

    for (i = 0; i < MAX_FRAME_WORDS; i++) {
        if (rx[i] != tx[i]) { match = false; break; }
    }

    if (match) g_loopbackOk++;
    else        g_loopbackErrors++;
}

//
// ============ CS ISR (frame boundary detection) ============
//
// CS falling = transfer start (DMA already armed, nothing to do)
// CS rising  = transfer end (process received frame, prepare next TX)
//
// g_cs_active guards against spurious double-trigger interrupts.
// Without this guard, a single CS rising edge could cause the ISR to
// run twice: first invocation processes valid data and swaps buffers;
// second invocation swaps again and processes the now-empty buffer,
// producing exactly half-fail behaviour.
//
__interrupt void cs_isr(void)
{
    if (GPIO_readPin(DSP_SPI_GPIO_CS_MIRROR) == 0) {
        //
        // CS falling — transfer starting
        //
        if (!g_cs_active) {
            g_cs_active = true;
            g_stats.cs_falling_count++;
            // DMA is already armed and will handle data as SPICLK runs
        } else {
            g_stats.cs_edge_ignored++;
        }
    }
    else {
        //
        // CS rising — transfer complete
        //
        if (g_cs_active) {
            g_cs_active = false;
            g_stats.cs_rising_count++;

            stopDma();

            // Swap: g_rxDone now holds the just-received data
            swapRxBuffers();

            if (validateRxFrame(g_rxDone)) {
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
// ============ Public API Implementation ============
//

void SpiDma_init(TestMode_t mode)
{
    g_testMode = mode;

    memset(g_rxBufferA, 0, sizeof(g_rxBufferA));
    memset(g_rxBufferB, 0, sizeof(g_rxBufferB));
    memset(g_txBufferA, 0, sizeof(g_txBufferA));
    memset(g_txBufferB, 0, sizeof(g_txBufferB));
    memset((void *)&g_stats, 0, sizeof(g_stats));

    g_cs_active = false;

    configSpiPins();
    configSpi();
    configDmaChannels();
    configCsInterrupt();

    buildTestTxFrame(g_txActive, 0);
    g_testPatternCounter = 1;

    // Reset FIFOs to clear any stale trigger state before first frame
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

    for (k = 0; k < 6; k++) { pack_float(data->ref[k], (volatile uint16_t *)&buf[i], (volatile uint16_t *)&buf[i+1]); i += 2; }
    for (k = 0; k < 6; k++) { buf[i++] = lo16((uint32_t)data->pos[k]); buf[i++] = hi16((uint32_t)data->pos[k]); }
    for (k = 0; k < 6; k++) { pack_float(data->u[k], (volatile uint16_t *)&buf[i], (volatile uint16_t *)&buf[i+1]); i += 2; }

    buf[i++] = data->err_bitmap;
    buf[i++] = data->err_count;

    for (k = 0; k < 6; k++) buf[i++] = data->adc[k];
    for (k = 0; k < 4; k++) { pack_float(data->quat[k], (volatile uint16_t *)&buf[i], (volatile uint16_t *)&buf[i+1]); i += 2; }

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
// ============ Internal Loopback Test (Phase A/B) ============
//
static void runLoopbackTest(void)
{
    SPI_disableModule(SPIA_BASE);
    SPI_setConfig(SPIA_BASE, DEVICE_LSPCLK_FREQ,
                  SPI_PROT_POL1PHA1,
                  SPI_MODE_MASTER,
                  1000000,
                  16);
    if (g_testMode == TEST_MODE_INTERNAL_LOOPBACK) {
        SPI_enableLoopback(SPIA_BASE);
    } else {
        SPI_disableLoopback(SPIA_BASE);
    }
    SPI_enableFIFO(SPIA_BASE);
    SPI_resetTxFIFO(SPIA_BASE);
    SPI_resetRxFIFO(SPIA_BASE);
    SPI_setFIFOInterruptLevel(SPIA_BASE, SPI_FIFO_TX1, SPI_FIFO_RX1);
    SPI_enableTalk(SPIA_BASE);
    SPI_enableModule(SPIA_BASE);

    rearmDma();

    DEVICE_DELAY_US(2000);

    if (DMA_getRunStatusFlag(SPI_DMA_RX_CH)) {
        g_stats.dma_errors++;
        stopDma();
        GPIO_writePin(DEVICE_GPIO_PIN_LED2, 0);
        return;
    }

    stopDma();

    verifyLoopback(g_rxActive, g_txActive);

    if (g_loopbackErrors == 0) {
        GPIO_togglePin(DEVICE_GPIO_PIN_LED1);
    } else {
        GPIO_writePin(DEVICE_GPIO_PIN_LED2, 0);
    }

    g_stats.tx_frame_count++;
    g_stats.rx_frame_count++;

    validateRxFrame(g_rxActive);

    buildTestTxFrame(g_txActive, g_testPatternCounter++);
}

//
// ============ Main ============
//
void main(void)
{
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    SpiDma_init((TestMode_t)SPI_TEST_MODE);

    EINT;
    ERTM;

    if (g_testMode == TEST_MODE_INTERNAL_LOOPBACK) {
        //
        // Phase A: DSP self-clocked master loopback
        //
        while (1) {
            runLoopbackTest();
            DEVICE_DELAY_US(10000);
        }
    }
    else {
        //
        // Phase B / C: DSP slave, RPi provides CLK+CS
        // Frame counts only increment when RPi is connected.
        //
        while (1) {
            // Update standby TX buffer with incrementing sequence number
            buildTestTxFrame(g_txStandby, g_testPatternCounter++);
            g_txDataReady = true;

            DEVICE_DELAY_US(1000);
        }
    }
}

//
// End of File
//

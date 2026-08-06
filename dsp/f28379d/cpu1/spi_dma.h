#ifndef SPI_DMA_H
#define SPI_DMA_H

#include <stdbool.h>
#include <stdint.h>

//
// ============ Frame Format ============
//
// CRC algorithm: CRC-16/CCITT-FALSE
//   poly=0x1021, init=0xFFFF, RefIn=false, RefOut=false, XorOut=0
//   Bytes within each 16-bit word are fed high byte first (MSB-first),
//   matching SPI transmission bit order.
//   Compatible with standard CRC-16/CCITT libraries on the RPi side.
//

// TX frame (DSP -> RPi): 61 words total (padded to MAX_FRAME_WORDS=64)
//   [0..3]   4x dummy (0xFFFF)
//   [4]      Header: 0xAA55
//   [5]      Version: 0x0003
//   [6..7]   timestamp_us (lo16, hi16)
//   [8..19]  ref[6] (float packed, 2 words each)
//   [20..31] pos[6] (int32 lo/hi, 2 words each)
//   [32..43] u[6] (float packed, 2 words each)
//   [44]     err_bitmap (bits 0-5 = axis 0-5 error flags)
//   [45]     err_count (uint16, cumulative total errors)
//   [46..51] adc[6] (uint16 each)
//   [52..59] quat[4] = {w,x,y,z} (float packed, 2 words each)
//   [60]     CRC16   (over words [4..59])

// RX frame (RPi -> DSP): 17 words
//   [0]      Header: 0x55AA
//   [1]      Version: 0x0002
//   [2..3]   cmd (uint32 lo/hi)
//   [4..15]  ref[6] (float packed, 2 words each)
//   [16]     CRC16   (over words [0..15])

#define MAX_FRAME_WORDS         64

#define TX_FRAME_NUM_DUMMY      4
#define TX_FRAME_DATA_WORDS     57  // words [4..60]: header + version + payload + CRC
#define TX_FRAME_TOTAL          61  // TX_FRAME_NUM_DUMMY + TX_FRAME_DATA_WORDS
#define TX_FRAME_HEADER         0xAA55
#define TX_FRAME_VERSION        0x0003
#define TX_FRAME_DUMMY_WORD     0xFFFF

#define RX_FRAME_WORDS          17
#define RX_FRAME_HEADER         0x55AA
#define RX_FRAME_VERSION        0x0002

//
// ============ SPI Pin Definitions ============
//
#define DSP_SPI_GPIO_SIMO       58U
#define DSP_SPI_GPIO_SOMI       59U
#define DSP_SPI_GPIO_CLK        60U
#define DSP_SPI_GPIO_STE        61U
#define DSP_SPI_GPIO_CS_MIRROR  40U

//
// ============ DMA Configuration ============
//
#define SPI_DMA_RX_CH           DMA_CH1_BASE
#define SPI_DMA_TX_CH           DMA_CH2_BASE
#define SPI_DMA_RX_TRIGGER      110     // DMA_TRIGGER_SPIARX
#define SPI_DMA_TX_TRIGGER      109     // DMA_TRIGGER_SPIATX

//
// ============ Statistics ============
//
typedef struct {
    volatile uint32_t tx_frame_count;
    volatile uint32_t rx_frame_count;
    volatile uint32_t crc_errors;
    volatile uint32_t frame_errors;
    volatile uint32_t dma_errors;
    volatile uint32_t cs_rising_count;
    volatile uint32_t cs_falling_count;
    volatile uint32_t cs_edge_ignored;  // suppressed spurious/double-trigger edges
} SpiDmaStats_t;

//
// ============ TX Data Structure ============
//
typedef struct {
    uint32_t timestamp_us;
    float    ref[6];
    int32_t  pos[6];
    float    u[6];
    uint16_t err_bitmap;    // bits 0-5: axis 0-5 error flags
    uint16_t err_count;     // cumulative total error count across all axes
    uint16_t adc[6];
    float    quat[4];       // {w, x, y, z} IMU quaternion
} TxFrame_t;

//
// ============ RX Data Structure ============
//
typedef struct {
    uint32_t cmd;
    float    ref[6];
} RxFrame_t;

//
// ============ RX Callback ============
//
typedef void (*SpiDma_rxCallback)(const RxFrame_t *rx);

//
// ============ Public API ============
//

// Initialize DMA SPI subsystem (always slave mode, no loopback)
// Call after Device_init(), Device_initGPIO(), Interrupt_initModule/VectorTable
void SpiDma_init(void);

// Submit new TX data (double-buffered, safe to call from main loop)
void SpiDma_updateTx(const TxFrame_t *data);

// Register callback invoked from CS ISR on each successfully received frame
void SpiDma_setRxCallback(SpiDma_rxCallback fn);

// Snapshot current statistics
void SpiDma_getStats(SpiDmaStats_t *out);

// Get stats pointer for CCS watch window
volatile SpiDmaStats_t* SpiDma_getStatsPtr(void);

#endif // SPI_DMA_H

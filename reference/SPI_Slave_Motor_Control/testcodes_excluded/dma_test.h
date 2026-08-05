#ifndef DMA_TEST_H
#define DMA_TEST_H

#include <stdint.h>
#include <stdbool.h>

#define TX_FRAME_WORDS 22
#define RX_FRAME_WORDS 10
#define TOTAL_FRAME_WORDS 22

extern volatile uint16_t s_tx_buffer[TOTAL_FRAME_WORDS];
extern volatile uint16_t s_rx_buffer[RX_FRAME_WORDS];

extern const void *SPI_DMA_TxAddr;
extern const void *SPI_DMA_RxAddr;

// === DMA Test Functions ===
void dma_test_init(void);
void dma_test_tick(void);

// === Test Functions ===
void dma_test_trigger_tx(void);
void dma_test_trigger_rx(void);
void dma_test_run_loopback_test(void);

// === Debug Functions ===
uint32_t dma_test_getTxCount(void);
uint32_t dma_test_getRxCount(void);
uint32_t dma_test_getFrameCount(void);
uint16_t dma_test_getRxWord(uint32_t index);
uint16_t dma_test_getTxWord(uint32_t index);
bool dma_test_getDmaTxActive(void);
bool dma_test_getDmaRxActive(void);

// === SPI Status Functions ===
bool dma_test_getSpiEnabled(void);
uint16_t dma_test_getSpiTxFifoLevel(void);
uint16_t dma_test_getSpiRxFifoLevel(void);
bool dma_test_getSpiTxFifoEmpty(void);
bool dma_test_getSpiRxFifoEmpty(void);

#endif // DMA_TEST_H

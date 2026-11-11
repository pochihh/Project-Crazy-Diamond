#ifndef SPI_DMA_H
#define SPI_DMA_H
#include "../../common/protocol.h"
#define MAX_FRAME_WORDS PCD_WORDS
#define DSP_SPI_GPIO_SIMO 58U
#define DSP_SPI_GPIO_SOMI 59U
#define DSP_SPI_GPIO_CLK 60U
#define DSP_SPI_GPIO_STE 61U
#define DSP_SPI_GPIO_CS_MIRROR 40U
#define SPI_DMA_RX_CH DMA_CH1_BASE
#define SPI_DMA_TX_CH DMA_CH2_BASE
#define SPI_DMA_RX_TRIGGER 110
#define SPI_DMA_TX_TRIGGER 109

typedef struct {
    volatile uint32_t tx_frame_count,rx_frame_count,crc_errors,frame_errors,dma_errors;
    volatile uint32_t cs_rising_count,cs_falling_count,cs_edge_ignored;
} SpiDmaStats_t;
typedef PcdCommand RxFrame_t;
typedef PcdTelemetry TxFrame_t;
typedef void (*SpiDma_rxCallback)(const RxFrame_t *rx);
void SpiDma_init(void);
/* Call from a non-nesting ISR, so the CS ISR cannot swap a frame during packing. */
void SpiDma_updateTx(const TxFrame_t *data);
void SpiDma_setRxCallback(SpiDma_rxCallback fn);
void SpiDma_getStats(SpiDmaStats_t *out);
volatile SpiDmaStats_t* SpiDma_getStatsPtr(void);
#endif

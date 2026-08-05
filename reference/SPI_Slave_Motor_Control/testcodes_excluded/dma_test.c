#include "dma_test.h"
#include "board.h"
#include "device.h"
#include "driverlib.h" // includes DMA, SPI

// === Pin definitions for SPIA on F28379D (LaunchXL J2 header, pins 58–61) ===
#define DSP_SPI_GPIO_SIMO 58U // Pi MOSI <-> DSP SIMO
#define DSP_SPI_GPIO_SOMI 59U // Pi MISO <-> DSP SOMI
#define DSP_SPI_GPIO_CLK 60U  // Pi SCLK <-> DSP CLK
#define DSP_SPI_GPIO_STE 61U  // Pi CS   <-> DSP STE

#define DSP_SPI_PROTO SPI_PROT_POL1PHA1 // Mode 3

// === Test Data ===
#pragma DATA_SECTION(s_tx_buffer, ".dma_buffers")
volatile uint16_t s_tx_buffer[TOTAL_FRAME_WORDS] = {
    0xAA55, 0x0002, 0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x1111, 0x2222,
    0x3333, 0x4444, 0x5555, 0x6666, 0x7777, 0x8888, 0x9999, 0xAAAA,
    0xBBBB, 0xCCCC, 0xDDDD, 0xEEEE, 0xFFFF, 0x0000};

#pragma DATA_SECTION(s_rx_buffer, ".dma_buffers")
volatile uint16_t s_rx_buffer[RX_FRAME_WORDS];

// === Global variables required by SysConfig-generated code ===
const void *SPI_DMA_TxAddr = (const void *)s_tx_buffer;
const void *SPI_DMA_RxAddr = (const void *)s_rx_buffer;

// === Debug Counters ===
static volatile uint32_t s_debug_tx_count = 0;
static volatile uint32_t s_debug_rx_count = 0;
static volatile uint32_t s_debug_frame_count = 0;

// === Public Functions ===
void dma_test_init(void)
{
    // Start the DMA channels
    DMA_startChannel(mySPI0_TX_DMA_BASE);
    DMA_startChannel(mySPI0_RX_DMA_BASE);

    // Debug: Check if channels are actually running
    // You can watch these variables in debugger
    volatile bool tx_running = DMA_getRunStatusFlag(mySPI0_TX_DMA_BASE);
    volatile bool rx_running = DMA_getRunStatusFlag(mySPI0_RX_DMA_BASE);
}

void dma_test_tick(void)
{
    // Main loop - DMA handles everything automatically
    // No CPU intervention needed for SPI transfers
}

// === Test Functions ===
void dma_test_trigger_tx(void)
{
    // Manually trigger TX DMA for testing
    DMA_forceTrigger(mySPI0_TX_DMA_BASE);
}

void dma_test_trigger_rx(void)
{
    // Manually trigger RX DMA for testing
    DMA_forceTrigger(mySPI0_RX_DMA_BASE);
}

void dma_test_run_loopback_test(void)
{
    // Run a complete loopback test
    int i;

    // Debug: Check SPI status before triggering
    // volatile bool spi_enabled = (HWREGH(mySPI0_BASE + SPI_O_CTL) & SPI_CTL_SPIENAS) != 0;
    volatile bool tx_dma_running = DMA_getRunStatusFlag(mySPI0_TX_DMA_BASE);
    volatile bool rx_dma_running = DMA_getRunStatusFlag(mySPI0_RX_DMA_BASE);

    // Trigger TX DMA first
    DMA_forceTrigger(mySPI0_TX_DMA_BASE);

    // Wait for TX to complete
    while (DMA_getRunStatusFlag(mySPI0_TX_DMA_BASE))
    {
        // Wait for TX DMA to finish
    }

    // Small delay
    for (i = 0; i < 1000; i++)
    {
        ;
    }

    // Trigger RX DMA
    DMA_forceTrigger(mySPI0_RX_DMA_BASE);

}

// === Debug Functions ===
uint32_t dma_test_getTxCount(void) { return s_debug_tx_count; }
uint32_t dma_test_getRxCount(void) { return s_debug_rx_count; }
uint32_t dma_test_getFrameCount(void) { return s_debug_frame_count; }
uint16_t dma_test_getRxWord(uint32_t index)
{
    return (index < RX_FRAME_WORDS) ? s_rx_buffer[index] : 0;
}
uint16_t dma_test_getTxWord(uint32_t index)
{
    return (index < TX_FRAME_WORDS) ? s_tx_buffer[index] : 0;
}

bool dma_test_getDmaTxActive(void)
{
    return DMA_getRunStatusFlag(mySPI0_TX_DMA_BASE);
}
bool dma_test_getDmaRxActive(void)
{
    return DMA_getRunStatusFlag(mySPI0_RX_DMA_BASE);
}

// === SPI Status Functions ===
bool dma_test_getSpiEnabled(void)
{
    // return (HWREGH(mySPI0_BASE + SPI_O_CTL) & SPI_CTL_SPIENAS) != 0;
    return true;
}

uint16_t dma_test_getSpiTxFifoLevel(void)
{
    return (HWREGH(mySPI0_BASE + SPI_O_FFTX) & SPI_FFTX_TXFFIL_M) >> SPI_FFTX_TXFFIL_S;
}

uint16_t dma_test_getSpiRxFifoLevel(void)
{
    return (HWREGH(mySPI0_BASE + SPI_O_FFRX) & SPI_FFRX_RXFFIL_M) >> SPI_FFRX_RXFFIL_S;
}

bool dma_test_getSpiTxFifoEmpty(void)
{
    return (HWREGH(mySPI0_BASE + SPI_O_FFTX) & SPI_FFTX_TXFFST_M) == 0;
}

bool dma_test_getSpiRxFifoEmpty(void)
{
    return (HWREGH(mySPI0_BASE + SPI_O_FFRX) & SPI_FFRX_RXFFST_M) == 0;
}

// === ISRs ===
__interrupt void dma_tx_isr(void)
{
    s_debug_tx_count++;
    DMA_clearTriggerFlag(mySPI0_TX_DMA_BASE);
    Interrupt_clearACKGroup(INT_mySPI0_TX_DMA_INTERRUPT_ACK_GROUP);
}

__interrupt void dma_rx_isr(void)
{
    s_debug_rx_count++;
    s_debug_frame_count++; // Increment frame count on RX completion
    DMA_clearTriggerFlag(mySPI0_RX_DMA_BASE);
    Interrupt_clearACKGroup(INT_mySPI0_RX_DMA_INTERRUPT_ACK_GROUP);
}

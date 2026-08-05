//#############################################################################
//
// FILE:   rpi_spi_test.c
//
// TITLE:  Raspberry Pi SPI master test program for DSP DMA SPI slave
//
// DESCRIPTION:
//   Linux spidev-based test program that communicates with the F28379D
//   running spi_dma.c. Sends incrementing test frames and validates
//   responses. Reports throughput and error rates.
//
// BUILD:
//   gcc -O2 -o spi_test rpi_spi_test.c -lm
//
// USAGE:
//   sudo ./spi_test [device] [speed_hz] [num_frames]
//   sudo ./spi_test /dev/spidev0.0 1000000 1000
//   sudo ./spi_test                                    # defaults
//
//#############################################################################

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

//
// ============ Frame Constants (must match spi_dma.h) ============
//

#define MAX_FRAME_WORDS         64

// TX from RPi -> DSP (we send RX frame format)
#define RPI_TX_HEADER           0x55AA
#define RPI_TX_VERSION          0x0002
#define RPI_TX_WORDS            17      // header(1)+version(1)+cmd(2)+ref[6](12)+CRC(1)

// RX from DSP -> RPi (we receive TX frame format)
#define DSP_TX_HEADER           0xAA55
#define DSP_TX_VERSION          0x0003
#define DSP_TX_NUM_DUMMY        4
#define DSP_TX_DATA_WORDS       57      // words [4..60] including CRC

//
// ============ CRC-16/CCITT-FALSE ============
//
// poly=0x1021, init=0xFFFF, RefIn=false, RefOut=false, XorOut=0
// Each 16-bit word fed high byte first (MSB-first), matching DSP implementation.
//
static uint16_t crc16(const uint16_t *data, uint16_t n_words)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < n_words; i++) {
        uint8_t hi = (data[i] >> 8) & 0xFF;    // high byte first
        crc ^= ((uint16_t)hi << 8);
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        uint8_t lo = data[i] & 0xFF;            // then low byte
        crc ^= ((uint16_t)lo << 8);
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

//
// ============ Float packing (matching DSP) ============
//
static void pack_float(float f, uint16_t *lo, uint16_t *hi)
{
    union { float f; uint32_t u; } conv;
    conv.f = f;
    *lo = (uint16_t)(conv.u & 0xFFFF);
    *hi = (uint16_t)(conv.u >> 16);
}

static float unpack_float(uint16_t lo, uint16_t hi)
{
    union { float f; uint32_t u; } conv;
    conv.u = ((uint32_t)hi << 16) | lo;
    return conv.f;
}

//
// ============ Build TX frame (RPi -> DSP) ============
//
// Sends cmd=frame_number, ref[0..5]=0. Remaining words (after RPI_TX_WORDS) are
// zeros — DSP receives them but the RX validation only checks the first 17 words.
//
static void build_tx_frame(uint16_t *buf, uint32_t cmd, const float ref[6])
{
    memset(buf, 0, MAX_FRAME_WORDS * sizeof(uint16_t));

    int i = 0;
    buf[i++] = RPI_TX_HEADER;
    buf[i++] = RPI_TX_VERSION;
    buf[i++] = (uint16_t)(cmd & 0xFFFF);
    buf[i++] = (uint16_t)(cmd >> 16);

    int k;
    for (k = 0; k < 6; k++) {
        pack_float(ref[k], &buf[i], &buf[i + 1]);
        i += 2;
    }

    buf[i] = crc16(buf, RPI_TX_WORDS - 1);
}

//
// ============ Parse RX frame (DSP -> RPi) ============
//
typedef struct {
    bool     valid;
    uint32_t timestamp_us;
    float    ref[6];
    int32_t  pos[6];
    float    u[6];
    uint16_t err_bitmap;
    uint16_t err_count;
    uint16_t adc[6];
    float    quat[4];
} DspFrame_t;

static DspFrame_t parse_rx_frame(const uint16_t *buf)
{
    DspFrame_t frame = {0};

    // Find header (skip dummy words)
    int hdr = -1;
    for (int i = 0; i < MAX_FRAME_WORDS; i++) {
        if (buf[i] == DSP_TX_HEADER) { hdr = i; break; }
    }

    if (hdr < 0 || (hdr + DSP_TX_DATA_WORDS) > MAX_FRAME_WORDS)
        return frame;  // valid = false

    if (buf[hdr + 1] != DSP_TX_VERSION)
        return frame;

    // CRC over words [hdr .. hdr+DSP_TX_DATA_WORDS-2] (56 words)
    uint16_t expected_crc = crc16(&buf[hdr], DSP_TX_DATA_WORDS - 1);
    if (expected_crc != buf[hdr + DSP_TX_DATA_WORDS - 1])
        return frame;

    int p = hdr + 2;  // skip header + version
    frame.timestamp_us = ((uint32_t)buf[p + 1] << 16) | buf[p]; p += 2;

    int k;
    for (k = 0; k < 6; k++) { frame.ref[k] = unpack_float(buf[p], buf[p+1]); p += 2; }
    for (k = 0; k < 6; k++) { frame.pos[k] = (int32_t)(((uint32_t)buf[p+1] << 16) | buf[p]); p += 2; }
    for (k = 0; k < 6; k++) { frame.u[k]   = unpack_float(buf[p], buf[p+1]); p += 2; }

    frame.err_bitmap = buf[p++];
    frame.err_count  = buf[p++];

    for (k = 0; k < 6; k++) frame.adc[k] = buf[p++];
    for (k = 0; k < 4; k++) { frame.quat[k] = unpack_float(buf[p], buf[p+1]); p += 2; }

    frame.valid = true;
    return frame;
}

//
// ============ Get time in microseconds ============
//
static uint64_t time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

//
// ============ SPI Transfer ============
//
static uint8_t g_bits_per_word = 16;  // set during init, used by spi_transfer

static int spi_transfer(int fd, const uint16_t *tx, uint16_t *rx, int words)
{
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = (uint32_t)(words * 2),
        .speed_hz      = 0,
        .delay_usecs   = 0,
        .bits_per_word = g_bits_per_word,
        .cs_change     = 0,
    };

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
        perror("SPI_IOC_MESSAGE");
        return -1;
    }
    return 0;
}

//
// ============ Main ============
//
int main(int argc, char *argv[])
{
    const char *device = "/dev/spidev0.0";
    uint32_t speed = 1000000;
    uint32_t num_frames = 1000;

    if (argc > 1) device = argv[1];
    if (argc > 2) speed = (uint32_t)atoi(argv[2]);
    if (argc > 3) num_frames = (uint32_t)atoi(argv[3]);

    printf("=== RPi SPI Test ===\n");
    printf("Device:     %s\n", device);
    printf("Speed:      %u Hz\n", speed);
    printf("Frames:     %u\n", num_frames);

    int fd = open(device, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    uint8_t mode = SPI_MODE_1;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("SPI_IOC_WR_MODE");
        close(fd);
        return 1;
    }

    g_bits_per_word = 16;
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &g_bits_per_word) < 0) {
        g_bits_per_word = 8;
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &g_bits_per_word);
        printf("Warning: using 8-bit mode (16-bit not supported)\n");
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI_IOC_WR_MAX_SPEED_HZ");
        close(fd);
        return 1;
    }

    printf("SPI Mode:   %d\n", mode);
    printf("Bits/word:  %d\n", g_bits_per_word);
    printf("\n");

    uint16_t tx_buf[MAX_FRAME_WORDS];
    uint16_t rx_buf[MAX_FRAME_WORDS];
    float ref_zero[6] = {0};

    uint32_t valid_frames = 0;
    uint32_t crc_errors = 0;
    uint32_t no_header = 0;
    uint32_t transfer_errors = 0;
    uint32_t seq_errors = 0;
    uint32_t last_ts = 0;

    uint64_t start_time = time_us();

    for (uint32_t frame = 0; frame < num_frames; frame++) {
        build_tx_frame(tx_buf, frame, ref_zero);

        memset(rx_buf, 0, sizeof(rx_buf));
        if (spi_transfer(fd, tx_buf, rx_buf, MAX_FRAME_WORDS) < 0) {
            transfer_errors++;
            continue;
        }

        DspFrame_t dsp = parse_rx_frame(rx_buf);
        if (dsp.valid) {
            valid_frames++;

            // Check timestamp is monotonically incrementing
            if (frame > 0 && dsp.timestamp_us != last_ts + 1)
                seq_errors++;
            last_ts = dsp.timestamp_us;

            if (frame < 5 || (frame % 100 == 0)) {
                printf("[%5u] ts=%u pos=[%d,%d,%d,%d,%d,%d] adc=[%u,%u,%u,%u,%u,%u] "
                       "err_map=0x%02x quat=[%.3f,%.3f,%.3f,%.3f]\n",
                       frame, dsp.timestamp_us,
                       dsp.pos[0], dsp.pos[1], dsp.pos[2],
                       dsp.pos[3], dsp.pos[4], dsp.pos[5],
                       dsp.adc[0], dsp.adc[1], dsp.adc[2],
                       dsp.adc[3], dsp.adc[4], dsp.adc[5],
                       dsp.err_bitmap,
                       dsp.quat[0], dsp.quat[1], dsp.quat[2], dsp.quat[3]);
            }
        } else {
            bool found_header = false;
            for (int i = 0; i < MAX_FRAME_WORDS; i++) {
                if (rx_buf[i] == DSP_TX_HEADER) {
                    found_header = true;
                    uint16_t c = crc16(&rx_buf[i], DSP_TX_DATA_WORDS - 1);
                    if (c != rx_buf[i + DSP_TX_DATA_WORDS - 1])
                        crc_errors++;
                    break;
                }
            }
            if (!found_header) no_header++;
        }

        usleep(100);
    }

    uint64_t elapsed_us = time_us() - start_time;
    double elapsed_s = elapsed_us / 1e6;
    double frame_rate = num_frames / elapsed_s;

    printf("\n=== Results ===\n");
    printf("Elapsed:         %.3f s\n", elapsed_s);
    printf("Frame rate:      %.1f Hz\n", frame_rate);
    printf("Valid frames:    %u / %u (%.1f%%)\n",
           valid_frames, num_frames, 100.0 * valid_frames / num_frames);
    printf("CRC errors:      %u\n", crc_errors);
    printf("No header:       %u\n", no_header);
    printf("Seq errors:      %u\n", seq_errors);
    printf("Transfer errors: %u\n", transfer_errors);
    printf("Throughput:      %.1f KB/s\n",
           (num_frames * MAX_FRAME_WORDS * 2.0) / 1024.0 / elapsed_s);

    close(fd);
    return (valid_frames == num_frames) ? 0 : 1;
}

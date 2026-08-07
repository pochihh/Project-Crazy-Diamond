// Raspberry Pi SPI bring-up test for the currently implemented DSP v2/v3 frames.
// Build: g++ -std=c++17 -O2 -Wall -Wextra -pedantic rpi_spi_test.cpp -o rpi_spi_test
// Run:   ./rpi_spi_test --motor-power-off [/dev/spidev0.0] [speed_hz] [frames] [mode] [rate_hz]
//
// This is intentionally not the planned v4/80-word production protocol. The
// current DSP ignores cmd and interprets all references as position targets, so
// keep motor power disconnected while using this communication-only test.

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <linux/spi/spidev.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace {

constexpr std::size_t kFrameWords = 64;
constexpr std::size_t kCommandWords = 17;
constexpr std::size_t kTelemetryWords = 57;
constexpr std::uint16_t kCommandHeader = 0x55AA;
constexpr std::uint16_t kCommandVersion = 2;
constexpr std::uint16_t kTelemetryHeader = 0xAA55;
constexpr std::uint16_t kTelemetryVersion = 3;

using Words = std::array<std::uint16_t, kFrameWords>;
using Clock = std::chrono::steady_clock;

Clock::time_point next_start(Clock::time_point scheduled_start,
                             Clock::duration period,
                             Clock::time_point now,
                             bool& deadline_missed)
{
    scheduled_start += period;
    deadline_missed = now > scheduled_start;
    return deadline_missed ? now + period : scheduled_start;
}

std::uint16_t crc16_byte(std::uint16_t crc, std::uint8_t byte)
{
    crc ^= static_cast<std::uint16_t>(byte) << 8;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000U) != 0U
                  ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021U)
                  : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

std::uint16_t crc16_bytes(const std::uint8_t* data, std::size_t size)
{
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc = crc16_byte(crc, data[i]);
    }
    return crc;
}

std::uint16_t crc16_words(const std::uint16_t* words, std::size_t count)
{
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t i = 0; i < count; ++i) {
        crc = crc16_byte(crc, static_cast<std::uint8_t>(words[i] >> 8));
        crc = crc16_byte(crc, static_cast<std::uint8_t>(words[i]));
    }
    return crc;
}

std::uint32_t float_bits(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float bits_float(std::uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void put_u32(Words& words, std::size_t& offset, std::uint32_t value)
{
    words[offset++] = static_cast<std::uint16_t>(value);
    words[offset++] = static_cast<std::uint16_t>(value >> 16);
}

std::uint32_t get_u32(const Words& words, std::size_t& offset)
{
    const std::uint32_t value = words[offset] |
                                (static_cast<std::uint32_t>(words[offset + 1]) << 16);
    offset += 2;
    return value;
}

void build_command(Words& words, std::uint32_t sequence)
{
    words.fill(0);
    std::size_t offset = 0;
    words[offset++] = kCommandHeader;
    words[offset++] = kCommandVersion;
    put_u32(words, offset, sequence);
    for (int axis = 0; axis < 6; ++axis) {
        put_u32(words, offset, float_bits(0.0F));
    }
    words[kCommandWords - 1] = crc16_words(words.data(), kCommandWords - 1);
}

struct Telemetry {
    std::size_t header_offset = 0;
    std::uint32_t timestamp_us = 0;
    std::array<float, 6> reference{};
    std::array<std::int32_t, 6> position{};
    std::array<float, 6> duty{};
    std::uint16_t error_bitmap = 0;
    std::uint16_t error_count = 0;
    std::array<std::uint16_t, 6> adc{};
    std::array<float, 4> quaternion{};
};

enum class ParseError { none, no_header, wrong_version, bad_crc };

ParseError parse_telemetry(const Words& words, Telemetry& telemetry)
{
    bool found_header = false;
    bool found_version = false;

    for (std::size_t header = 0; header + kTelemetryWords <= words.size(); ++header) {
        if (words[header] != kTelemetryHeader) {
            continue;
        }
        found_header = true;
        if (words[header + 1] != kTelemetryVersion) {
            continue;
        }
        found_version = true;
        if (crc16_words(words.data() + header, kTelemetryWords - 1) !=
            words[header + kTelemetryWords - 1]) {
            continue;
        }

        telemetry.header_offset = header;
        std::size_t offset = header + 2;
        telemetry.timestamp_us = get_u32(words, offset);
        for (float& value : telemetry.reference) {
            value = bits_float(get_u32(words, offset));
        }
        for (std::int32_t& value : telemetry.position) {
            value = static_cast<std::int32_t>(get_u32(words, offset));
        }
        for (float& value : telemetry.duty) {
            value = bits_float(get_u32(words, offset));
        }
        telemetry.error_bitmap = words[offset++];
        telemetry.error_count = words[offset++];
        for (std::uint16_t& value : telemetry.adc) {
            value = words[offset++];
        }
        for (float& value : telemetry.quaternion) {
            value = bits_float(get_u32(words, offset));
        }
        return ParseError::none;
    }

    if (found_version) {
        return ParseError::bad_crc;
    }
    return found_header ? ParseError::wrong_version : ParseError::no_header;
}

bool self_test()
{
    static constexpr std::uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    if (crc16_bytes(check, sizeof(check)) != 0x29B1U) {
        return false;
    }

    Words command{};
    build_command(command, 0x12345678U);
    if (command[2] != 0x5678U || command[3] != 0x1234U ||
        crc16_words(command.data(), kCommandWords - 1) != command[16]) {
        return false;
    }

    const auto epoch = Clock::time_point{};
    const auto period = std::chrono::milliseconds(1);
    bool missed = false;
    if (next_start(epoch, period, epoch, missed) != epoch + period || missed) {
        return false;
    }
    return next_start(epoch, period, epoch + period * 2, missed) ==
               epoch + period * 3 &&
           missed;
}

int transfer(int fd, std::uint32_t speed_hz, const Words& tx, Words& rx)
{
    spi_ioc_transfer transfer{};
    transfer.tx_buf = reinterpret_cast<std::uintptr_t>(tx.data());
    transfer.rx_buf = reinterpret_cast<std::uintptr_t>(rx.data());
    transfer.len = static_cast<std::uint32_t>(tx.size() * sizeof(tx[0]));
    transfer.speed_hz = speed_hz;
    transfer.bits_per_word = 16;
    return ioctl(fd, SPI_IOC_MESSAGE(1), &transfer);
}

void print_words(const Words& words)
{
    std::cerr << "First invalid RX words:";
    for (std::size_t i = 0; i < 16; ++i) {
        std::cerr << ' ' << std::hex << std::setw(4) << std::setfill('0') << words[i];
    }
    std::cerr << std::dec << std::setfill(' ') << '\n';
}

std::uint32_t parse_number(const char* text, const char* name,
                           std::uint32_t minimum, std::uint32_t maximum)
{
    std::size_t parsed = 0;
    const unsigned long value = std::stoul(text, &parsed, 10);
    if (text[parsed] != '\0' || value < minimum || value > maximum) {
        throw std::invalid_argument(std::string(name) + " is out of range");
    }
    return static_cast<std::uint32_t>(value);
}

void usage(const char* program)
{
    std::cerr << "Usage:\n  " << program << " --self-test\n  " << program
              << " --motor-power-off [device] [speed_hz] [frames] [mode] [rate_hz]\n";
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        const bool ok = self_test();
        std::cout << (ok ? "self-test: PASS\n" : "self-test: FAIL\n");
        return ok ? 0 : 1;
    }
    if (argc > 7 || argc < 2 || std::string(argv[1]) != "--motor-power-off") {
        usage(argv[0]);
        return 2;
    }

    try {
        const char* device = argc > 2 ? argv[2] : "/dev/spidev0.0";
        const std::uint32_t speed_hz = argc > 3
                                           ? parse_number(argv[3], "speed_hz", 10'000, 20'000'000)
                                           : 1'000'000;
        const std::uint32_t frame_count = argc > 4
                                              ? parse_number(argv[4], "frames", 1, 1'000'000)
                                              : 100;
        std::uint8_t mode = argc > 5
                                ? static_cast<std::uint8_t>(
                                      parse_number(argv[5], "mode", 0, 3))
                                : SPI_MODE_1;
        const std::uint32_t rate_hz = argc > 6
                                          ? parse_number(argv[6], "rate_hz", 1, 100'000)
                                          : 1'000;
        const auto period = std::chrono::nanoseconds(1'000'000'000ULL / rate_hz);

        const int fd = open(device, O_RDWR);
        if (fd < 0) {
            throw std::runtime_error("cannot open " + std::string(device) + ": " +
                                     std::strerror(errno));
        }

        std::uint8_t bits = 16;
        if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
            ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
            ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0) {
            const std::string error = std::strerror(errno);
            close(fd);
            throw std::runtime_error("cannot configure SPI: " + error);
        }

        std::cout << "Legacy DSP SPI bring-up: " << device << ", mode "
                  << static_cast<unsigned>(mode) << ", "
                  << speed_hz << " Hz, " << rate_hz << " Hz target, "
                  << frame_count << " measured frames\n";

        std::array<std::uint32_t, 4> results{};
        std::uint32_t transfer_errors = 0;
        std::uint32_t timestamp_errors = 0;
        std::uint32_t previous_timestamp = 0;
        bool have_timestamp = false;
        bool dumped_invalid = false;
        std::uint32_t deadline_misses = 0;
        Clock::duration max_start_lateness{};
        Clock::duration max_transfer{};
        Clock::duration total_transfer{};
        Clock::time_point first_start;
        Clock::time_point last_start;
        bool have_start = false;
        auto scheduled_start = Clock::now();

        // Two pipeline warm-up transactions are intentionally not scored.
        for (std::uint32_t frame = 0; frame < frame_count + 2; ++frame) {
            const bool measured = frame >= 2;
            const auto frame_start = Clock::now();
            if (measured) {
                if (!have_start) {
                    first_start = frame_start;
                    have_start = true;
                }
                last_start = frame_start;
                if (frame_start > scheduled_start) {
                    const auto lateness = frame_start - scheduled_start;
                    if (lateness > max_start_lateness) {
                        max_start_lateness = lateness;
                    }
                }
            }

            Words tx_words{};
            build_command(tx_words, frame);
            Words rx_words{};

            const auto transfer_start = Clock::now();
            const bool transfer_ok = transfer(fd, speed_hz, tx_words, rx_words) >= 0;
            const auto transfer_time = Clock::now() - transfer_start;
            if (measured) {
                total_transfer += transfer_time;
                if (transfer_time > max_transfer) {
                    max_transfer = transfer_time;
                }
            }

            if (!transfer_ok) {
                if (measured) {
                    ++transfer_errors;
                }
            } else if (measured) {
                Telemetry telemetry;
                const ParseError error = parse_telemetry(rx_words, telemetry);
                ++results[static_cast<std::size_t>(error)];
                if (error != ParseError::none) {
                    if (!dumped_invalid) {
                        print_words(rx_words);
                        dumped_invalid = true;
                    }
                } else {
                    if (have_timestamp &&
                        static_cast<std::int32_t>(telemetry.timestamp_us -
                                                  previous_timestamp) <= 0) {
                        ++timestamp_errors;
                    }
                    previous_timestamp = telemetry.timestamp_us;
                    have_timestamp = true;

                    const std::uint32_t measured_frame = frame - 2;
                    if (measured_frame < 5 || measured_frame % 1000 == 0) {
                        std::cout << '[' << measured_frame << "] ts_us="
                                  << telemetry.timestamp_us << " header_word="
                                  << telemetry.header_offset << " pos=[";
                        for (std::size_t axis = 0; axis < telemetry.position.size(); ++axis) {
                            std::cout << (axis == 0 ? "" : ",")
                                      << telemetry.position[axis];
                        }
                        std::cout << "] duty=[";
                        for (std::size_t axis = 0; axis < telemetry.duty.size(); ++axis) {
                            std::cout << (axis == 0 ? "" : ",") << telemetry.duty[axis];
                        }
                        std::cout << "] err=0x" << std::hex << telemetry.error_bitmap
                                  << std::dec << '\n';
                    }
                }
            }

            bool deadline_missed = false;
            scheduled_start = next_start(scheduled_start, period, Clock::now(),
                                         deadline_missed);
            if (measured && deadline_missed) {
                ++deadline_misses;
            }
            std::this_thread::sleep_until(scheduled_start);
        }
        close(fd);

        const double actual_rate_hz = frame_count > 1
                                          ? (frame_count - 1) /
                                                std::chrono::duration<double>(
                                                    last_start - first_start)
                                                    .count()
                                          : 0.0;
        const std::uint32_t valid = results[static_cast<std::size_t>(ParseError::none)];
        std::cout << "Results: valid=" << valid << '/' << frame_count
                  << " no_header=" << results[static_cast<std::size_t>(ParseError::no_header)]
                  << " wrong_version=" << results[static_cast<std::size_t>(ParseError::wrong_version)]
                  << " bad_crc=" << results[static_cast<std::size_t>(ParseError::bad_crc)]
                  << " transfer_errors=" << transfer_errors
                  << " timestamp_errors=" << timestamp_errors
                  << " deadline_misses=" << deadline_misses
                  << " rate=" << std::fixed << std::setprecision(1) << actual_rate_hz
                  << " Hz max_start_late="
                  << std::chrono::duration<double, std::micro>(max_start_lateness).count()
                  << " us transfer_avg="
                  << std::chrono::duration<double, std::micro>(total_transfer).count() /
                         frame_count
                  << " us transfer_max="
                  << std::chrono::duration<double, std::micro>(max_transfer).count()
                  << " us\n";

        return valid == frame_count && timestamp_errors == 0 && transfer_errors == 0 &&
                       deadline_misses == 0
                   ? 0
                   : 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}

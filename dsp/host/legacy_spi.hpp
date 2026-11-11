#pragma once
// Shared working v2/v3 codec; keep firmware and bench compatibility in stage 2.
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef __linux__
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace diamond {
constexpr std::size_t kAxes = 6;
constexpr std::size_t kFrameWords = 64;
constexpr std::size_t kCommandWords = 17;
constexpr std::size_t kTelemetryWords = 57;
constexpr std::uint16_t kCommandHeader = 0x55AA;
constexpr std::uint16_t kCommandVersion = 2;
constexpr std::uint16_t kTelemetryHeader = 0xAA55;
constexpr std::uint16_t kTelemetryVersion = 3;
constexpr std::uint32_t kDutyCommand = 0x44555459U;
constexpr std::uint32_t kDisarmCommand = 0x53544F50U;
constexpr std::uint32_t kArmCommand = 0x41524D00U;
constexpr std::uint32_t kSpiSpeedHz = 10'000'000;
constexpr std::uint32_t kRateHz = 1'000;
constexpr int kRequiredCpu = 3;
constexpr double kPi = 3.14159265358979323846;

using Words = std::array<std::uint16_t, kFrameWords>;
using Values = std::array<float, kAxes>;
using Clock = std::chrono::steady_clock;

class InvalidTelemetryFrame final : public std::runtime_error {
public:
    InvalidTelemetryFrame() : std::runtime_error("invalid DSP telemetry frame") {}
};

inline std::uint16_t crc16_byte(std::uint16_t crc, std::uint8_t byte)
{
    crc ^= static_cast<std::uint16_t>(byte) << 8;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000U) != 0U
                  ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021U)
                  : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

inline std::uint16_t crc16_words(const std::uint16_t* words, std::size_t count)
{
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t i = 0; i < count; ++i) {
        crc = crc16_byte(crc, static_cast<std::uint8_t>(words[i] >> 8));
        crc = crc16_byte(crc, static_cast<std::uint8_t>(words[i]));
    }
    return crc;
}

inline std::uint32_t float_bits(float value)
{
    std::uint32_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float bits_float(std::uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline void put_u32(Words& words, std::size_t& offset, std::uint32_t value)
{
    words[offset++] = static_cast<std::uint16_t>(value);
    words[offset++] = static_cast<std::uint16_t>(value >> 16);
}

inline std::uint32_t get_u32(const Words& words, std::size_t& offset)
{
    const auto value = static_cast<std::uint32_t>(words[offset]) |
                       (static_cast<std::uint32_t>(words[offset + 1]) << 16);
    offset += 2;
    return value;
}

inline Words make_command(std::uint32_t command, const Values& values = {})
{
    Words words{};
    std::size_t offset = 0;
    words[offset++] = kCommandHeader;
    words[offset++] = kCommandVersion;
    put_u32(words, offset, command);
    for (float value : values) put_u32(words, offset, float_bits(value));
    words[kCommandWords - 1] = crc16_words(words.data(), kCommandWords - 1);
    return words;
}

struct Telemetry {
    std::uint32_t timestamp_us = 0;
    Values requested_duty{};
    std::array<std::int32_t, kAxes> position{};
    Values duty{};
};

inline Telemetry parse_telemetry(const Words& words)
{
    for (std::size_t header = 0; header + kTelemetryWords <= words.size(); ++header) {
        if (words[header] != kTelemetryHeader ||
            words[header + 1] != kTelemetryVersion) {
            continue;
        }
        if (crc16_words(words.data() + header, kTelemetryWords - 1) !=
            words[header + kTelemetryWords - 1]) {
            continue;
        }

        Telemetry telemetry;
        std::size_t offset = header + 2;
        telemetry.timestamp_us = get_u32(words, offset);
        for (float& value : telemetry.requested_duty) {
            value = bits_float(get_u32(words, offset));
        }
        for (auto& value : telemetry.position) {
            value = static_cast<std::int32_t>(get_u32(words, offset));
        }
        for (float& value : telemetry.duty) {
            value = bits_float(get_u32(words, offset));
        }
        return telemetry;
    }
    throw InvalidTelemetryFrame();
}

#ifdef __linux__
class SpiLink {
public:
    explicit SpiLink(const char* device)
    {
        fd_ = open(device, O_RDWR);
        if (fd_ < 0) {
            throw std::runtime_error("cannot open " + std::string(device) + ": " +
                                     std::strerror(errno));
        }
        if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            close(fd_);
            fd_ = -1;
            throw std::runtime_error("SPI device is already owned by another process");
        }
        std::uint8_t mode = SPI_MODE_1;
        std::uint8_t bits = 16;
        std::uint32_t speed = kSpiSpeedHz;
        if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 ||
            ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
            ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
            const std::string error = std::strerror(errno);
            close(fd_);
            fd_ = -1;
            throw std::runtime_error("cannot configure SPI: " + error);
        }
    }

    ~SpiLink()
    {
        disarm();
        if (fd_ >= 0) close(fd_);
    }

    SpiLink(const SpiLink&) = delete;
    SpiLink& operator=(const SpiLink&) = delete;

    Telemetry exchange(std::uint32_t command, const Values& values = {})
    {
        const Words tx = make_command(command, values);
        Words rx{};
        spi_ioc_transfer transfer{};
        transfer.tx_buf = reinterpret_cast<std::uintptr_t>(tx.data());
        transfer.rx_buf = reinterpret_cast<std::uintptr_t>(rx.data());
        transfer.len = static_cast<std::uint32_t>(tx.size() * sizeof(tx[0]));
        transfer.speed_hz = kSpiSpeedHz;
        transfer.bits_per_word = 16;
        if (ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer) < 0) {
            throw std::runtime_error("SPI transfer failed: " +
                                     std::string(std::strerror(errno)));
        }
        const Telemetry telemetry = parse_telemetry(rx);
        if (have_timestamp_ &&
            static_cast<std::int32_t>(telemetry.timestamp_us - last_timestamp_) <= 0) {
            throw std::runtime_error("stale DSP telemetry timestamp");
        }
        last_timestamp_ = telemetry.timestamp_us;
        have_timestamp_ = true;
        return telemetry;
    }

    void disarm() noexcept
    {
        if (fd_ < 0) return;
        const Words tx = make_command(kDisarmCommand);
        for (int attempt = 0; attempt < 5; ++attempt) {
            Words rx{};
            spi_ioc_transfer transfer{};
            transfer.tx_buf = reinterpret_cast<std::uintptr_t>(tx.data());
            transfer.rx_buf = reinterpret_cast<std::uintptr_t>(rx.data());
            transfer.len = static_cast<std::uint32_t>(tx.size() * sizeof(tx[0]));
            transfer.speed_hz = kSpiSpeedHz;
            transfer.bits_per_word = 16;
            static_cast<void>(ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    int fd_ = -1;
    std::uint32_t last_timestamp_ = 0;
    bool have_timestamp_ = false;
};

#endif
}

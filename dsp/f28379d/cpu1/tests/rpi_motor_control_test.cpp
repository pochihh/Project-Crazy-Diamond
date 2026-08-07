// Raspberry Pi motor bring-up for the temporary DSP v2/v3 SPI protocol.
// Build:
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic
//       rpi_motor_control_test.cpp -o rpi_motor_control_test
// Run only on the isolated RT CPU:
//   sudo taskset -c 3 chrt -f 80 ./rpi_motor_control_test --enable-motors ...
//
// First verify each H-bridge direction with --probe. Only then use --sine.

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <linux/spi/spidev.h>
#include <sched.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace {

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

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int)
{
    g_stop = 1;
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
    std::uint32_t bits;
    static_assert(sizeof(bits) == sizeof(value));
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
    const auto value = static_cast<std::uint32_t>(words[offset]) |
                       (static_cast<std::uint32_t>(words[offset + 1]) << 16);
    offset += 2;
    return value;
}

Words make_command(std::uint32_t command, const Values& values = {})
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

Telemetry parse_telemetry(const Words& words)
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
    throw std::runtime_error("invalid DSP telemetry frame");
}

class SpiLink {
public:
    explicit SpiLink(const char* device)
    {
        fd_ = open(device, O_RDWR);
        if (fd_ < 0) {
            throw std::runtime_error("cannot open " + std::string(device) + ": " +
                                     std::strerror(errno));
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

struct AxisTuning {
    double kp = 0.0;
    double ki = 0.0;
    double kd = 0.0;
    double amplitude = 0.0;
    double sign = 1.0;
    double max_duty = 1.0;
};

// ============ Normal sine-run defaults (edit these to tune) ============
constexpr AxisTuning kAxis0Defaults{0.0005, 0.001, 0.0, 2000.0, -1.0, 1.0};
constexpr AxisTuning kAxis1Defaults{0.0005, 0.001, 0.0, 2000.0, -1.0, 1.0};
constexpr double kSinePeriodSeconds = 1.0;
constexpr double kSineDurationSeconds = 0.0;  // 0 = continuous until Ctrl-C

struct Pid {
    explicit Pid(const AxisTuning& tuning) : tuning(tuning) {}

    double step(double error, double dt)
    {
        const double derivative = have_previous ? (error - previous_error) / dt : 0.0;
        const double raw = tuning.kp * error + integral_term + tuning.kd * derivative;
        if (!std::isfinite(raw)) throw std::runtime_error("PID output is not finite");
        const double output = std::clamp(raw, -tuning.max_duty, tuning.max_duty);
        integral_term += tuning.ki * error * dt + (output - raw);
        previous_error = error;
        have_previous = true;
        return tuning.sign * output;
    }

    AxisTuning tuning;
    double integral_term = 0.0;
    double previous_error = 0.0;
    bool have_previous = false;
};

struct Options {
    bool enable_motors = false;
    bool sine = false;
    std::uint16_t probe_mask = 0;
    double probe_duty = 0.0;
    int probe_ms = 250;
    std::array<AxisTuning, 2> axis{kAxis0Defaults, kAxis1Defaults};
    double period_s = kSinePeriodSeconds;
    double duration_s = kSineDurationSeconds;
    std::string device = "/dev/spidev0.0";
};

double number(const char* text, const char* name)
{
    std::size_t parsed = 0;
    const double value = std::stod(text, &parsed);
    if (text[parsed] != '\0' || !std::isfinite(value)) {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return value;
}

int integer(const char* text, const char* name)
{
    std::size_t parsed = 0;
    const int value = std::stoi(text, &parsed, 10);
    if (text[parsed] != '\0') {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return value;
}

void usage(const char* program)
{
    std::cerr
        << "Usage:\n"
        << "  " << program << " --self-test\n"
        << "  sudo taskset -c 3 chrt -f 80 " << program
        << " --enable-motors --probe-axis N [--probe-axis N ...]"
           " --probe-duty D [--probe-ms MS]\n"
        << "  sudo taskset -c 3 chrt -f 80 " << program
        << " --enable-motors --sine\n"
        << "    Optional overrides: --kp0 X --ki0 X --kd0 X --amp0 COUNTS"
           " --sign0 +/-1 --max-duty0 X --kp1 X --ki1 X --kd1 X"
           " --amp1 COUNTS --sign1 +/-1 --max-duty1 X"
           " [--period SEC] [--duration SEC]\n";
}

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (++i >= argc) throw std::invalid_argument(arg + " needs a value");
            return argv[i];
        };

        if (arg == "--enable-motors") options.enable_motors = true;
        else if (arg == "--sine") options.sine = true;
        else if (arg == "--probe-axis") {
            const int axis = integer(value(), "probe axis");
            if (axis < 0 || axis > 5) throw std::invalid_argument("probe axis must be 0-5");
            options.probe_mask |= static_cast<std::uint16_t>(1U << axis);
        }
        else if (arg == "--probe-duty") options.probe_duty = number(value(), "probe duty");
        else if (arg == "--probe-ms") options.probe_ms = integer(value(), "probe duration");
        else if (arg == "--kp0") options.axis[0].kp = number(value(), "kp0");
        else if (arg == "--ki0") options.axis[0].ki = number(value(), "ki0");
        else if (arg == "--kd0") options.axis[0].kd = number(value(), "kd0");
        else if (arg == "--amp0") options.axis[0].amplitude = number(value(), "amp0");
        else if (arg == "--sign0") options.axis[0].sign = number(value(), "sign0");
        else if (arg == "--max-duty0") options.axis[0].max_duty = number(value(), "max-duty0");
        else if (arg == "--kp1") options.axis[1].kp = number(value(), "kp1");
        else if (arg == "--ki1") options.axis[1].ki = number(value(), "ki1");
        else if (arg == "--kd1") options.axis[1].kd = number(value(), "kd1");
        else if (arg == "--amp1") options.axis[1].amplitude = number(value(), "amp1");
        else if (arg == "--sign1") options.axis[1].sign = number(value(), "sign1");
        else if (arg == "--max-duty1") options.axis[1].max_duty = number(value(), "max-duty1");
        else if (arg == "--period") options.period_s = number(value(), "period");
        else if (arg == "--duration") options.duration_s = number(value(), "duration");
        else if (arg == "--device") options.device = value();
        else throw std::invalid_argument("unknown option: " + arg);
    }
    return options;
}

void validate(const Options& options)
{
    if (!options.enable_motors) {
        throw std::invalid_argument("--enable-motors is required");
    }
    if (options.sine == (options.probe_mask != 0U)) {
        throw std::invalid_argument("select exactly one of --sine or --probe-axis");
    }
    if (options.probe_mask != 0U) {
        if (std::abs(options.probe_duty) > 0.1 || options.probe_duty == 0.0 ||
            (options.probe_ms != 0 && (options.probe_ms < 10 || options.probe_ms > 60000))) {
            throw std::invalid_argument("probe requires nonzero |duty| <= 0.1 and 10-60000 ms (0 is continuous)");
        }
        return;
    }
    if (options.period_s < 0.1 || options.period_s > 60.0 ||
        options.duration_s < 0.0 || options.duration_s > 3600.0) {
        throw std::invalid_argument("period must be 0.1-60 s and duration must be 0-3600 s (0 is continuous)");
    }
    for (const auto& axis : options.axis) {
        if ((axis.sign != -1.0 && axis.sign != 1.0) || axis.amplitude < 0.0 ||
            axis.amplitude > 10000.0 ||
            axis.max_duty <= 0.0 || axis.max_duty > 1.0) {
            throw std::invalid_argument("sine requires sign +/-1, amplitude 0-10000 counts, and 0 < max duty <= 1");
        }
    }
}

void require_realtime()
{
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    if (sched_getaffinity(0, sizeof(cpus), &cpus) != 0 ||
        CPU_COUNT(&cpus) != 1 || !CPU_ISSET(kRequiredCpu, &cpus) ||
        sched_getscheduler(0) != SCHED_FIFO) {
        throw std::runtime_error("run with: sudo taskset -c 3 chrt -f 80 ...");
    }
}

Clock::time_point wait_next(Clock::time_point scheduled,
                            Clock::duration period)
{
    const auto next = scheduled + period;
    if (Clock::now() > next) throw std::runtime_error("1 kHz control deadline missed");
    std::this_thread::sleep_until(next);
    return next;
}

Telemetry get_initial_telemetry(SpiLink& link)
{
    Telemetry telemetry;
    for (int frame = 0; frame < 10; ++frame) {
        telemetry = link.exchange(kDisarmCommand);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return telemetry;
}

void run_probe(SpiLink& link, const Options& options, Telemetry telemetry)
{
    const auto initial = telemetry.position;
    Values duty{};
    for (std::size_t axis = 0; axis < duty.size(); ++axis) {
        if ((options.probe_mask & (1U << axis)) != 0U) {
            duty[axis] = static_cast<float>(options.probe_duty);
        }
    }

    link.exchange(kArmCommand | options.probe_mask);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto scheduled = Clock::now();
    const auto period = std::chrono::microseconds(1'000);
    for (int frame = 0; (options.probe_ms == 0 || frame < options.probe_ms) && !g_stop; ++frame) {
        telemetry = link.exchange(kDutyCommand, duty);
        scheduled = wait_next(scheduled, period);
    }

    link.disarm();
    for (std::size_t axis = 0; axis < duty.size(); ++axis) {
        if ((options.probe_mask & (1U << axis)) == 0U) continue;
        const auto delta = telemetry.position[axis] - initial[axis];
        std::cout << "Probe axis " << axis << ": duty=" << options.probe_duty
                  << " accepted=" << telemetry.requested_duty[axis]
                  << " applied=" << telemetry.duty[axis]
                  << " start=" << initial[axis] << " end=" << telemetry.position[axis]
                  << " delta=" << delta << " counts\n";
    }
}

void run_sine(SpiLink& link, const Options& options, Telemetry telemetry)
{
    const std::array<std::int32_t, 2> center = {
        telemetry.position[0], telemetry.position[1]
    };
    std::array<Pid, 2> pid = {Pid(options.axis[0]), Pid(options.axis[1])};

    link.exchange(kArmCommand | 0x03U);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Values duty{};
    auto scheduled = Clock::now();
    const auto period = std::chrono::microseconds(1'000);
    const auto start = scheduled;
    std::uint32_t frame = 0;

    while (!g_stop) {
        const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        if (options.duration_s > 0.0 && elapsed >= options.duration_s) break;
        const double sine = std::sin(2.0 * kPi * elapsed / options.period_s);

        std::array<double, 2> target{};
        for (std::size_t axis = 0; axis < 2; ++axis) {
            target[axis] = center[axis] + options.axis[axis].amplitude * sine;
            const double error = target[axis] - telemetry.position[axis];
            const double max_error = std::max(100.0, 2.0 * options.axis[axis].amplitude);
            if (std::abs(error) > max_error) {
                throw std::runtime_error("axis " + std::to_string(axis) +
                                         " tracking error limit exceeded");
            }
            duty[axis] = static_cast<float>(pid[axis].step(error, 1.0 / kRateHz));
        }

        telemetry = link.exchange(kDutyCommand, duty);
        if (frame % 100U == 0U) {
            std::cout << std::fixed << std::setprecision(1)
                      << "t=" << elapsed << " target_rel=["
                      << target[0] - center[0] << ',' << target[1] - center[1]
                      << "] pos_rel=["
                      << telemetry.position[0] - center[0] << ','
                      << telemetry.position[1] - center[1]
                      << "] duty=[" << std::setprecision(4) << duty[0] << ',' << duty[1]
                      << "]\n";
        }
        ++frame;
        scheduled = wait_next(scheduled, period);
    }
    link.disarm();
}

bool self_test()
{
    const Options defaults;
    if (defaults.axis[0].amplitude != 2000.0 ||
        defaults.axis[1].amplitude != 2000.0 ||
        defaults.period_s != 1.0 || defaults.duration_s != 0.0) {
        return false;
    }

    const Words command = make_command(kDutyCommand, {0.25F, -0.25F});
    if (command[0] != kCommandHeader || command[1] != kCommandVersion ||
        command[2] != 0x5459U || command[3] != 0x4455U ||
        crc16_words(command.data(), kCommandWords - 1) != command[16]) {
        return false;
    }

    AxisTuning tuning;
    tuning.kp = 1.0;
    tuning.max_duty = 0.1;
    Pid pid(tuning);
    return std::abs(pid.step(1.0, 0.001) - 0.1) < 1e-9;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        const bool passed = self_test();
        std::cout << (passed ? "self-test: PASS\n" : "self-test: FAIL\n");
        return passed ? 0 : 1;
    }

    try {
        const Options options = parse_options(argc, argv);
        validate(options);
        require_realtime();
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);

        SpiLink link(options.device.c_str());
        const Telemetry telemetry = get_initial_telemetry(link);
        if (options.probe_mask != 0U) run_probe(link, options, telemetry);
        else run_sine(link, options, telemetry);
        return 0;
    } catch (const std::exception& error) {
        usage(argv[0]);
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

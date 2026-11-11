#pragma once
// Active PCD v5 transport. The legacy bench codec is retained only for recovery.
#include "../common/protocol.h"
#include <algorithm>
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
constexpr std::size_t kAxes=PCD_AXES,kFrameWords=PCD_WORDS;
constexpr std::uint32_t kSpiSpeedHz=10'000'000,kRateHz=2'000;
constexpr int kRequiredCpu=3;
constexpr double kPi=3.14159265358979323846;
constexpr uint16_t kDutyCommand=PCD_CYCLE,kArmCommand=PCD_ARM,kDisarmCommand=PCD_DISARM;
using Words=std::array<uint16_t,kFrameWords>;
using Values=std::array<float,kAxes>;
using Clock=std::chrono::steady_clock;
using Telemetry=PcdTelemetry;
struct SpiTransferInfo {
    Words tx{},rx{};
    uint64_t duration_ns=0;
    int result=0,error=0;
};
class InvalidTelemetryFrame final:public std::runtime_error {
public:InvalidTelemetryFrame():std::runtime_error("Invalid v5 DSP telemetry"){}
};
inline Words make_command(const PcdCommand& command){Words w{};pcd_encode_command(w.data(),&command);return w;}
inline Telemetry parse_telemetry(const Words& words){
    constexpr auto length=PCD_TX_CRC-PCD_TX_START+1;
    for(std::size_t h=0;h+length<=words.size();h++){
        if(words[h]!=PCD_TELEMETRY_HEADER||words[h+1]!=PCD_VERSION)continue;
        Words aligned{};std::copy_n(words.begin()+h,length,aligned.begin()+PCD_TX_START);
        Telemetry t{};if(pcd_decode_telemetry(aligned.data(),&t))return t;
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
    const SpiTransferInfo& diagnostics() const {return last_transfer_;}

    Telemetry exchange(uint16_t operation,const Values& values={},uint16_t mask=0,
                       const std::array<int32_t,6>& reference={},uint16_t digital_out=0,
                       const std::array<uint16_t,2>& analog_out={}) {
        PcdCommand c{};c.operation=synchronized_?operation:PCD_SYNC;
        c.session=session_;c.sequence=++sequence_;c.arm_mask=mask;c.digital_out=digital_out;
        std::copy(values.begin(),values.end(),c.duty);
        std::copy(reference.begin(),reference.end(),c.reference);
        std::copy(analog_out.begin(),analog_out.end(),c.analog_out);
        const auto telemetry=parse_telemetry(transfer(make_command(c)));
        if(have_timestamp_ && (static_cast<int32_t>(telemetry.timestamp_us-last_timestamp_)<=0 ||
                              static_cast<int32_t>(telemetry.sequence-last_frame_)<=0))
            throw std::runtime_error("Stale DSP telemetry");
        last_timestamp_=telemetry.timestamp_us;last_frame_=telemetry.sequence;have_timestamp_=true;
        if(telemetry.session==session_){
            synchronized_=true;
            if(telemetry.command_sequence!=last_ack_){last_ack_=telemetry.command_sequence;ack_time_=Clock::now();}
            if(Clock::now()-ack_time_>std::chrono::milliseconds(20))throw std::runtime_error("DSP stopped acknowledging commands");
        } else if(synchronized_) throw std::runtime_error("DSP session changed; restart bridge disarmed");
        return telemetry;
    }
    void disarm() noexcept {
        if(fd_<0)return;
        for(int n=0;n<5;n++){
            PcdCommand c{};c.operation=PCD_DISARM;c.session=session_;c.sequence=++sequence_;
            try{transfer(make_command(c));}catch(...){}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    Words transfer(const Words& tx) {
        Words rx{};spi_ioc_transfer transfer{};
        transfer.tx_buf=reinterpret_cast<std::uintptr_t>(tx.data());
        transfer.rx_buf=reinterpret_cast<std::uintptr_t>(rx.data());
        transfer.len=static_cast<uint32_t>(tx.size()*sizeof(tx[0]));
        transfer.speed_hz=kSpiSpeedHz;transfer.bits_per_word=16;
        const auto begin=Clock::now();
        const int result=ioctl(fd_,SPI_IOC_MESSAGE(1),&transfer);
        const int error=result<0?errno:0;
        last_transfer_={tx,rx,static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count()),result,error};
        if(result<0)throw std::runtime_error(std::string("SPI transfer failed: ")+std::strerror(error));
        if(static_cast<uint32_t>(result)!=transfer.len)throw std::runtime_error("Short SPI transfer");
        return rx;
    }
    int fd_ = -1;
    uint32_t session_=static_cast<uint32_t>(Clock::now().time_since_epoch().count())|1U;
    uint32_t sequence_=0,last_ack_=0,last_frame_=0;
    bool synchronized_=false;
    Clock::time_point ack_time_=Clock::now();
    std::uint32_t last_timestamp_ = 0;
    bool have_timestamp_ = false;
    SpiTransferInfo last_transfer_{};
};

#endif
}

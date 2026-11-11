#pragma once
#include "spi.hpp"
#include <iomanip>
#include <ostream>

namespace diamond {
constexpr auto control_period=std::chrono::microseconds(1'000'000/kRateHz);
inline uint64_t missed_periods(Clock::time_point scheduled,Clock::time_point finished) {
    const auto elapsed=finished-scheduled;
    return elapsed>control_period?static_cast<uint64_t>((elapsed-Clock::duration{1})/control_period):0;
}
struct CycleTrace {
    uint64_t number=0,host_ns=0;
    double work_us=0,late_us=0,spi_us=0;
    uint32_t frame=0,dsp_us=0,ack=0;
    uint16_t operation=0,mask=0,commands=0;
};
struct RealtimeEvent {
    std::array<CycleTrace,8> history{};
    unsigned count=0;
    bool deadline=false;
    uint64_t missed_periods=0;
    std::array<char,160> error{};
    SpiTransferInfo spi{};
};
class CycleHistory {
public:
    void push(const CycleTrace& trace) {
        traces_[next_]=trace;next_=(next_+1)%traces_.size();
        if(count_<traces_.size())++count_;
    }
    void copy(RealtimeEvent& event) const {
        event.count=count_;
        for(unsigned i=0;i<count_;++i)event.history[i]=traces_[(next_+traces_.size()-count_+i)%traces_.size()];
    }
private:
    std::array<CycleTrace,8> traces_{};
    unsigned next_=0,count_=0;
};
// Called only by the ordinary ROS thread; the control thread queues fixed-size data.
inline void write_event(std::ostream& out,const RealtimeEvent& event) {
    out<<"{\"deadline\":"<<(event.deadline?"true":"false")<<",\"missed_periods\":"<<event.missed_periods
       <<",\"period_us\":"<<control_period.count()<<",\"error\":"<<std::quoted(event.error.data())
       <<",\"ioctl_result\":"<<event.spi.result<<",\"errno\":"<<event.spi.error<<",\"history\":[";
    for(unsigned i=0;i<event.count;++i) {
        const auto& t=event.history[i];
        out<<(i?",":"")<<"{\"cycle\":"<<t.number<<",\"host_ns\":"<<t.host_ns<<",\"work_us\":"<<t.work_us
           <<",\"late_us\":"<<t.late_us<<",\"spi_us\":"<<t.spi_us<<",\"frame\":"<<t.frame
           <<",\"dsp_us\":"<<t.dsp_us<<",\"ack\":"<<t.ack<<",\"operation\":"<<t.operation
           <<",\"mask\":"<<t.mask<<",\"commands\":"<<t.commands<<'}';
    }
    out<<"],\"tx\":[";
    for(unsigned i=0;i<event.spi.tx.size();++i)out<<(i?",":"")<<event.spi.tx[i];
    out<<"],\"rx\":[";
    for(unsigned i=0;i<event.spi.rx.size();++i)out<<(i?",":"")<<event.spi.rx[i];
    out<<"]}\n";
}
}

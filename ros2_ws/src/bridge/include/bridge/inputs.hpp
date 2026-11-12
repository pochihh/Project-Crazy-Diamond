#pragma once
#include "control.hpp"
#include "latest.hpp"

namespace diamond {
// Latest value per input channel. Discrete commands keep FIFO ordering; each
// continuous input is applied once, ordered against those commands by arrival.
class Inputs {
public:
    static constexpr unsigned count=19;
    using Snapshot=std::array<Command,count>;
    void publish(Command command) {
        command.input=true;
        unsigned slot=count-1; // Invalid channel: retain it for RT rejection/reporting.
        if(command.op==Op::target && command.axis<6)slot=command.axis;
        if(command.op==Op::pwm && command.axis<6)slot=6+command.axis;
        if(command.op==Op::digital && command.axis<4)slot=12+command.axis;
        if(command.op==Op::analog && command.axis<2)slot=16+command.axis;
        staged_[slot]=command;
        latest_.publish(staged_);
    }
    unsigned take(Command* out) {
        Snapshot snapshot;
        if(!latest_.take(snapshot))return 0;
        unsigned n=0;
        for(unsigned i=0;i<count;i++)if(snapshot[i].order>seen_[i]) {
            seen_[i]=snapshot[i].order;out[n++]=snapshot[i];
        }
        return n;
    }
private:
    Snapshot staged_{};
    Latest<Snapshot> latest_;
    std::array<uint64_t,count> seen_{};
};
}

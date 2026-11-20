#pragma once
#include <array>
#include <cstdint>

namespace diamond {
struct ControllerSignals {
    double kp=0,r=0,y=0,error=0,p=0,i=0,d=0,u=0,injection=0,plant=0;
};
struct ControlFrame {
    std::array<ControllerSignals,6> axes{};
    uint32_t input_sequence=0;
    uint16_t valid_mask=0;
    uint64_t record=0,host_tick=0;
    uint16_t operation=0,arm_mask=0,digital_out=0;
    std::array<uint16_t,2> analog_out{};
    std::array<float,6> sent_duty{};
    int64_t experiment_tick=-1;
};
// SPI returns an already prepared DSP snapshot. Pair on the echoed key, including
// across duplicate acknowledgements and 32-bit wrap, never by host tick offset.
class ControlHistory {
    struct Entry {uint32_t session=0,sequence=0; ControlFrame frame{};};
    std::array<Entry,64> entries_{};
public:
    void put(uint32_t session,uint32_t sequence,const ControlFrame& frame) {
        entries_[sequence%entries_.size()]={session,sequence,frame};
    }
    ControlFrame find(uint32_t session,uint32_t sequence) const {
        const auto& entry=entries_[sequence%entries_.size()];
        return session && entry.session==session && entry.sequence==sequence ? entry.frame : ControlFrame{};
    }
};
}

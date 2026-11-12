#pragma once
#include "control.hpp"
#include <vector>

namespace diamond {
// Immutable once handed to the worker. Storage is owned/reclaimed by the ROS thread.
struct Program {
    uint64_t id=0;
    std::string name;
    std::vector<uint8_t> channels;
    std::vector<double> values;
    uint16_t axes=0,digital=0,analog=0;
    size_t rows() const {return channels.empty()?0:values.size()/channels.size();}
};
inline bool valid_value(uint8_t channel,double value,const Sample& s) {
    if(!std::isfinite(value))return false;
    if(channel<6)return s.position_configured[channel]&&value>=s.minimum[channel]&&value<=s.maximum[channel];
    if(channel<12)return std::abs(value)<=s.tuning[channel-6].limit;
    return value==std::floor(value)&&value>=0&&value<=(channel<16?1:4095);
}
inline void validate_program(Program& p,const Sample& s) {
    if(p.name.empty()||p.name.size()>100||p.name.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Invalid program name");
    if(p.channels.empty()||p.channels.size()>18||p.values.empty()||p.values.size()%p.channels.size()||p.values.size()>2'000'000)
        throw std::runtime_error("Program needs complete rows and at most 2,000,000 values");
    uint32_t seen=0;
    for(auto c:p.channels) {
        if(c>=18||(seen&(1U<<c)))throw std::runtime_error("Unknown or duplicate command channel");
        seen|=1U<<c;
        if(c<12){if(p.axes&(1U<<(c%6)))throw std::runtime_error("Reference and duty cannot control the same axis");p.axes|=1U<<(c%6);}
        else if(c<16)p.digital|=1U<<(c-12);else p.analog|=1U<<(c-16);
    }
    for(size_t i=0;i<p.values.size();i++)if(!valid_value(p.channels[i%p.channels.size()],p.values[i],s))
        throw std::runtime_error("Program value is outside the configured range at row "+std::to_string(i/p.channels.size()+1));
}
class Runner {
    const Program* program_=nullptr;
    uint64_t record_=0;
    size_t next_=0;
public:
    bool owns(const Sample& s) const {return s.runner_state==1||s.runner_state==2;}
    bool conflicts(const Command& c,const Sample& s) const {
        if(!owns(s))return false;
        if(c.op==Op::target||c.op==Op::pwm||c.op==Op::mode||c.op==Op::zero||c.op==Op::limit)
            return c.axis<6&&(s.runner_axes&(1U<<c.axis));
        if(c.op==Op::digital)return c.axis<4&&(s.runner_digital&(1U<<c.axis));
        if(c.op==Op::analog)return c.axis<2&&(s.runner_analog&(1U<<c.axis));
        return false;
    }
    void stop(Control& core,uint8_t state=3,Error error=Error::none) {
        auto& s=core.state;
        if(owns(s)) {
            for(unsigned a=0;a<6;a++)if(s.runner_axes&(1U<<a))core.apply({Op::axis,a,0,false});
            s.digital_out&=~s.runner_digital;
            for(unsigned a=0;a<2;a++)if(s.runner_analog&(1U<<a))s.analog_out[a]=0;
            if(record_&&s.record_id==record_)s.record_id=0;
        }
        s.runner_state=state;s.runner_error=error;record_=0;
    }
    Error command(const Command& c,Control& core) {
        auto& s=core.state;
        if(c.op==Op::runner_stop){stop(core);return Error::none;}
        if(c.op==Op::runner_load) {
            if(owns(s))return Error::busy;
            program_=c.program;s.program_id=program_->id;s.runner_rows=program_->rows();
            s.runner_axes=program_->axes;s.runner_digital=program_->digital;s.runner_analog=program_->analog;
            s.runner_state=0;s.runner_row=0;s.runner_cycles=0;s.runner_error=Error::none;return Error::none;
        }
        if(!program_||owns(s))return Error::busy;
        if(!s.connected||s.fault!=Error::none||core.dsp_clear_requested)return Error::not_ready;
        if(c.value<0||c.value>2)return Error::range;
        // Stage the mode/enable changes so a rejected Start changes no output selection.
        Control next=core;
        if(c.enabled&&!s.system)next.state.selected={};
        for(auto channel:program_->channels)if(channel<12) {
            const unsigned a=channel%6,mode=channel<6?0:1;
            if(next.state.mode[a]!=mode){next.apply({Op::mode,a,double(mode)});if(next.state.command_error!=Error::none)return next.state.command_error;}
            if(c.enabled){next.apply({Op::axis,a,0,true});if(next.state.command_error!=Error::none)return next.state.command_error;}
            else if(!next.enabled(a))return Error::not_ready;
        }
        if(c.enabled){next.apply({Op::system,0,0,true});if(next.state.command_error!=Error::none)return next.state.command_error;}
        if(!next.state.system)return Error::not_ready;
        core=next;s.runner_state=1;s.runner_completion=static_cast<uint8_t>(c.value);
        s.runner_row=0;s.runner_cycles=0;s.runner_error=Error::none;next_=0;record_=c.record;
        if(record_)s.record_id=record_;
        return Error::none;
    }
    void tick(Control& core) {
        auto& s=core.state;
        if(!owns(s))return;
        if(!s.connected||s.fault!=Error::none||!s.system||(core.mask()&s.runner_axes)!=s.runner_axes) {
            stop(core,4,s.fault==Error::none?Error::not_ready:s.fault);return;
        }
        if(s.runner_state==2)return;
        if(next_==program_->rows()) {
            ++s.runner_cycles;
            if(s.runner_completion==0){s.runner_state=2;return;}
            if(s.runner_completion==1){stop(core);return;}
            next_=0;
        }
        const auto offset=next_*program_->channels.size();
        for(size_t i=0;i<program_->channels.size();i++)if(!valid_value(program_->channels[i],program_->values[offset+i],s)) {
            stop(core,4,Error::range);return;
        }
        for(size_t i=0;i<program_->channels.size();i++) {
            const auto c=program_->channels[i];const auto v=program_->values[offset+i];
            if(c<6)s.target[c]=v;
            else if(c<12)s.pwm[c-6]=v;
            else if(c<16)s.digital_out=(s.digital_out&~(1U<<(c-12)))|(static_cast<uint16_t>(v)<<(c-12));
            else s.analog_out[c-16]=static_cast<uint16_t>(v);
        }
        s.runner_row=next_++;
    }
};
}

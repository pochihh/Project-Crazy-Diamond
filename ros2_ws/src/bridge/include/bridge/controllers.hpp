#pragma once
#include "pid.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <variant>

namespace diamond {
constexpr unsigned max_controller_parameters=16;
using ProfileId=std::array<char,64>;
struct Parameter { const char *name,*unit; double initial,minimum,maximum; };
struct ControllerType { const char *id,*label; unsigned count; std::array<Parameter,max_controller_parameters> parameters; };
inline constexpr std::array<ControllerType,2> controller_types={{
    {"pid","PID",4,{{{"kp","duty/count",0.0005,0,1e6},{"ki","duty/(count·s)",0.001,0,1e6},{"kd","duty·s/count",0,0,1e6},{"d_filter_hz","Hz (0 = bypass)",0,0,1000}}}},
    {"step_recorder","Step Recorder",5,{{{"kp","duty/count",0.0005,0,1e6},{"step_duty","signed duty",0.05,-1,1},
        {"baseline_s","s",0.5,0,3600},{"step_s","s",0.25,0.0005,3600},{"recovery_s","s",0.5,0,3600}}}}
}};
constexpr uint8_t step_recorder_type=1;
struct ControllerConfig {
    uint8_t type=0;
    bool slew_enabled=false;
    double slew_rate_per_s=20;
    std::array<double,max_controller_parameters> parameters{0.0005,0.001,0,0};
};
inline bool valid_controller(const ControllerConfig& c) {
    if(c.type>=controller_types.size() || !std::isfinite(c.slew_rate_per_s) || c.slew_rate_per_s<=0)return false;
    if(c.type==step_recorder_type && c.slew_enabled)return false;
    const auto& type=controller_types[c.type];
    for(unsigned i=0;i<c.parameters.size();i++) {
        const auto value=c.parameters[i];
        if(!std::isfinite(value))return false;
        if(i<type.count) {if(value<type.parameters[i].minimum||value>type.parameters[i].maximum)return false;}
        else if(value!=0)return false;
    }
    return true;
}
inline uint8_t controller_index(const std::string& id) {
    for(unsigned i=0;i<controller_types.size();i++)if(id==controller_types[i].id)return i;
    throw std::runtime_error("Controller type is not compiled in: "+id);
}
inline ProfileId profile_id(const std::string& id) {
    if(id.empty()||id.size()>63||id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)
        throw std::runtime_error("Profile ID must use 1–63 lowercase letters, digits, underscores or hyphens");
    ProfileId out{};std::copy(id.begin(),id.end(),out.begin());return out;
}
struct StepRecorderController {
    void reset() {}
    double step(ControllerSignals& signals,double gain,double sign,double limit,double injection) const {
        signals.p=sign*gain*signals.error;
        signals.u=signals.p;
        signals.injection=injection;
        return std::clamp(signals.u+injection,-limit,limit);
    }
};
class AxisController {
    // Add the new implementation here and its descriptor above when rebuilding.
    std::variant<PidController,StepRecorderController> implementation_;
    bool have_reference_=false;
public:
    ControllerSignals signals{};
    ControllerConfig config{};
    double sign=-1,limit=1,reference=0;
    void reset(){have_reference_=false;std::visit([](auto& p){p.reset();},implementation_);}
    Tuning tuning() const {
        if(config.type==step_recorder_type)return {config.parameters[0],0,0,sign,limit,0};
        return {config.parameters[0],config.parameters[1],config.parameters[2],sign,limit,config.parameters[3]};
    }
    void configure(const ControllerConfig& next,bool running) {
        if(!valid_controller(next))throw std::runtime_error("Invalid controller configuration");
        if(next.type!=config.type) {
            if(next.type==step_recorder_type)implementation_.emplace<StepRecorderController>();
            else implementation_.emplace<PidController>();
            have_reference_=false;running=false;
        }
        config=next;
        if(auto* pid=std::get_if<PidController>(&implementation_))pid->retune(tuning(),running);
    }
    double step(double target,double position,double counts_per_unit,double dt,double injection=0) {
        if(!have_reference_){reference=position;have_reference_=true;}
        reference=config.slew_enabled ? reference+std::clamp(target-reference,-config.slew_rate_per_s*dt,config.slew_rate_per_s*dt) : target;
        if(config.type==step_recorder_type)reference=0;
        const double error=(reference-position)*counts_per_unit;
        signals={};signals.r=reference;signals.y=position;signals.error=error;
        signals.kp=config.parameters[0];
        if(auto* step=std::get_if<StepRecorderController>(&implementation_))return step->step(signals,signals.kp,sign,limit,injection);
        auto& pid=std::get<PidController>(implementation_);
        pid.tuning.sign=sign;pid.tuning.limit=limit;
        signals.u=pid.step(error,dt,&signals);
        return signals.u;
    }
};
}

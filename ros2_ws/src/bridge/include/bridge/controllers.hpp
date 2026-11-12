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
inline constexpr std::array<ControllerType,1> controller_types={{
    {"pid","PID",3,{{{"kp","duty/count",0.0005,0,1e6},{"ki","duty/(count·s)",0.001,0,1e6},{"kd","duty·s/count",0,0,1e6}}}}
}};
struct ControllerConfig {
    uint8_t type=0;
    bool slew_enabled=false;
    double slew_rate_mm_s=20;
    std::array<double,max_controller_parameters> parameters{0.0005,0.001,0};
};
inline bool valid_controller(const ControllerConfig& c) {
    if(c.type>=controller_types.size() || !std::isfinite(c.slew_rate_mm_s) || c.slew_rate_mm_s<=0)return false;
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
class AxisController {
    // Add the new implementation here and its descriptor above when rebuilding.
    std::variant<PidController> implementation_;
    bool have_reference_=false;
public:
    ControllerConfig config{};
    double sign=-1,limit=1,reference_mm=0;
    void reset(){have_reference_=false;std::visit([](auto& p){p.reset();},implementation_);}
    Tuning tuning() const {return {config.parameters[0],config.parameters[1],config.parameters[2],sign,limit};}
    void configure(const ControllerConfig& next,bool running) {
        if(!valid_controller(next))throw std::runtime_error("Invalid controller configuration");
        if(next.type!=config.type) {implementation_.emplace<PidController>();running=false;}
        config=next;
        std::get<PidController>(implementation_).retune(tuning(),running);
    }
    double step(double target_mm,double position_mm,double counts_per_mm,double dt) {
        if(!have_reference_){reference_mm=position_mm;have_reference_=true;}
        reference_mm=config.slew_enabled ? reference_mm+std::clamp(target_mm-reference_mm,-config.slew_rate_mm_s*dt,config.slew_rate_mm_s*dt) : target_mm;
        const double error=(reference_mm-position_mm)*counts_per_mm;
        return std::visit([&](auto& p){p.tuning.sign=sign;p.tuning.limit=limit;return p.step(error,dt);},implementation_);
    }
};
}

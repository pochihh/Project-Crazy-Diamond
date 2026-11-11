#pragma once
#include "spi.hpp"
#include "controllers.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace diamond {
constexpr double counts_per_mm = (500.0 * 4 * (91.0 / 6)) / (30 * kPi);
constexpr double dt = 1.0/kRateHz;

template<class T, std::size_t N> class Queue {
    std::array<T, N> data_{};
    std::atomic<std::size_t> write_{0}, read_{0};
public:
    bool push(const T& value) {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto next = (w + 1) % N;
        if (next == read_.load(std::memory_order_acquire)) return false;
        data_[w] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& value) {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        value = data_[r];
        read_.store((r + 1) % N, std::memory_order_release);
        return true;
    }
};

enum class Op { target, pwm, mode, axis, system, stop, zero, clear, kp, ki, kd, limit, digital, analog, record, controller, profile };
enum class Error { none, disabled_required, range, zero_required, wrong_mode, travel, not_ready, tracking, numeric, deadline, telemetry, dsp, realtime };
inline const char* error_text(Error e) {
    switch(e) {
    case Error::none: return "";
    case Error::disabled_required: return "Disable this axis before changing mode, controller type, zero, or duty limit";
    case Error::range: return "Value is outside the configured range or duty limit";
    case Error::zero_required: return "Measured position is outside travel range; reset zero before enabling";
    case Error::wrong_mode: return "Command does not match the selected control mode";
    case Error::travel: return "PWM motion exceeded the configured position range";
    case Error::not_ready: return "Stop and clear the host fault before enabling";
    case Error::tracking: return "Tracking error exceeded configured limit";
    case Error::numeric: return "Non-finite control or telemetry value";
    case Error::deadline: return "2 kHz control deadline missed";
    case Error::telemetry: return "Invalid, stale, or unavailable DSP telemetry";
    case Error::dsp: return "DSP fault latched; stop and clear fault";
    case Error::realtime: return "Real-time scheduling or memory setup failed";
    }
    return "Unknown fault";
}
struct Command {
    Op op{}; unsigned axis{}; double value{}; bool enabled{}; uint64_t id{};
    ControllerConfig controller{}; ProfileId profile{};
};
struct Sample {
    Telemetry telemetry{};
    uint64_t number=0, timestamp=0, command_id=0, record_id=0;
    std::array<double,2> target{}, reference{}, position{};
    std::array<double,2> pwm{}, scale{counts_per_mm,counts_per_mm}, minimum{-50,-50}, maximum{50,50};
    std::array<uint8_t,2> mode{}; // 0: position, 1: direct normalized DSP duty.
    std::array<int32_t,2> zero{};
    std::array<bool,2> selected{};
    std::array<Tuning,2> tuning{};
    std::array<ControllerConfig,2> controllers{};
    std::array<ProfileId,2> profiles{};
    std::array<uint64_t,2> controller_revision{};
    uint16_t digital_out=0;
    std::array<uint16_t,2> analog_out{};
    bool system=false, connected=false;
    Error fault=Error::none, command_error=Error::none;
    uint64_t deadlines=0, errors=0, dropped=0;
    double max_cycle_us=0, max_lateness_us=0;
};
class Control {
public:
    Sample state{};
    std::array<AxisController,2> pid{};
    double speed_mm_s=20, tracking_limit_mm=15;
    bool initialized=false, dsp_clear_requested=false;
    uint16_t mask() const {
        if (!state.system || state.fault!=Error::none || !state.connected) return 0;
        return (state.selected[0]?1:0)|(state.selected[1]?2:0);
    }
    bool enabled(unsigned axis) const { return (mask() & (1U<<axis))!=0; }
    void receive(const Telemetry& t) {
        state.telemetry=t;
        if(t.fault && !dsp_clear_requested)fail(Error::dsp);
        state.connected=true;
        if (!initialized) {
            for(unsigned a=0;a<2;a++) state.zero[a]=t.position[a];
            state.timestamp=t.timestamp_us;
            initialized=true;
        } else state.timestamp+=static_cast<uint32_t>(t.timestamp_us-last_time_);
        last_time_=t.timestamp_us;
        for(unsigned a=0;a<2;a++) {
            state.position[a]=(static_cast<double>(t.position[a])-state.zero[a])/state.scale[a];
            state.tuning[a]=pid[a].tuning();
            state.controllers[a]=pid[a].config;
        }
        state.number++;
    }
    void fail(Error error) {
        state.fault=error; state.system=false; state.selected={false,false};
        state.pwm={0,0};state.digital_out=0;state.analog_out={0,0};
        for(auto& p:pid) p.reset();
    }
    void apply(const Command& c) {
        state.command_id=c.id; state.command_error=Error::none;
        const auto a=c.axis;
        const auto before=mask();
        if(c.op==Op::digital || c.op==Op::analog){
            if(!std::isfinite(c.value)||c.value<0||std::floor(c.value)!=c.value ||
               (c.op==Op::digital && (a>=4 || c.value>1)) ||
               (c.op==Op::analog && (a>=2 || c.value>4095))){state.command_error=Error::range;return;}
            if(!state.system||state.fault!=Error::none){state.command_error=Error::not_ready;return;}
            if(c.op==Op::digital)state.digital_out=(state.digital_out&~(1U<<a))|(static_cast<uint16_t>(c.value)<<a);
            else state.analog_out[a]=static_cast<uint16_t>(c.value);
            return;
        }
        if (a>=2) { state.command_error=Error::range; return; }
        switch(c.op) {
        case Op::stop: state.system=false; state.selected={false,false}; state.pwm={0,0}; state.digital_out=0;state.analog_out={0,0};break;
        case Op::clear:
            if (mask()!=0) { state.command_error=Error::disabled_required; return; }
            dsp_clear_requested=true;state.fault=Error::none; state.system=false; state.selected={false,false}; break;
        case Op::system:
        case Op::axis:
            if(c.enabled && (!state.connected || state.fault!=Error::none || dsp_clear_requested)) {
                state.command_error=Error::not_ready; return;
            }
            for(unsigned i=0;i<2;i++) {
                const bool activating=c.op==Op::system ? state.selected[i] : state.system && i==a;
                if(c.enabled && activating && (state.position[i]<state.minimum[i] || state.position[i]>state.maximum[i])) {
                    state.command_error=Error::zero_required; return;
                }
            }
            if(c.op==Op::system){state.system=c.enabled;if(!c.enabled){state.digital_out=0;state.analog_out={0,0};}}else state.selected[a]=c.enabled;
            break;
        case Op::target:
            if(state.mode[a]!=0) {state.command_error=Error::wrong_mode; return;}
            if(!std::isfinite(c.value) || c.value<state.minimum[a] || c.value>state.maximum[a]) {state.command_error=Error::range; return;}
            state.target[a]=c.value; break;
        case Op::pwm:
            if(state.mode[a]!=1) {state.command_error=Error::wrong_mode; return;}
            if(!std::isfinite(c.value) || std::abs(c.value)>pid[a].limit) {state.command_error=Error::range; return;}
            state.pwm[a]=c.value; break;
        case Op::mode:
            if(enabled(a)) {state.command_error=Error::disabled_required; return;}
            if(c.value!=0 && c.value!=1) {state.command_error=Error::range; return;}
            state.mode[a]=static_cast<uint8_t>(c.value);state.pwm[a]=0;
            state.target[a]=std::clamp(state.position[a],state.minimum[a],state.maximum[a]);
            state.reference[a]=state.position[a];pid[a].reset();break;
        case Op::zero:
            if(enabled(a)) {state.command_error=Error::disabled_required; return;}
            if(!state.connected) {state.command_error=Error::not_ready; return;}
            state.zero[a]=state.telemetry.position[a];
            state.position[a]=state.target[a]=state.reference[a]=0;
            state.pwm[a]=0;
            pid[a].reset(); break;
        case Op::record: state.record_id=static_cast<uint64_t>(c.value); break;
        case Op::controller:
            if(enabled(a)&&c.controller.type!=pid[a].config.type) {state.command_error=Error::disabled_required;return;}
            if(!valid_controller(c.controller)) {state.command_error=Error::range;return;}
            pid[a].configure(c.controller,enabled(a));
            if(c.profile[0])state.profiles[a]=c.profile;
            state.controller_revision[a]++;break;
        case Op::profile:
            state.profiles[a]=c.profile;state.controller_revision[a]++;break;
        default:
            if(c.op==Op::limit && enabled(a)) {state.command_error=Error::disabled_required; return;}
            if(!std::isfinite(c.value) || c.value<0 || (c.op==Op::limit && (c.value<=0 || c.value>1))) {
                state.command_error=Error::range; return;
            }
            if(c.op==Op::limit) {pid[a].limit=c.value;pid[a].reset();break;}
            if(pid[a].config.type!=0) {state.command_error=Error::wrong_mode;return;}
            auto candidate=pid[a].config;
            candidate.parameters[c.op==Op::kp?0:c.op==Op::ki?1:2]=c.value;
            if(!valid_controller(candidate)) {state.command_error=Error::range;return;}
            pid[a].configure(candidate,enabled(a));state.controller_revision[a]++;break;
        }
        const auto after=mask();
        for(unsigned i=0;i<2;i++) {
            if((before^after)&(1U<<i)) {
                pid[i].reset(); state.reference[i]=state.position[i];
                state.target[i]=std::clamp(state.position[i],state.minimum[i],state.maximum[i]);
                state.pwm[i]=0;
            }
        }
    }
    Values duty() {
        Values result{};
        for(unsigned a=0;a<2;a++) {
            if(!enabled(a)) {pid[a].reset(); continue;}
            if(state.mode[a]==1) {
                if(state.position[a]<state.minimum[a] || state.position[a]>state.maximum[a]) {fail(Error::travel);return {};}
                result[a]=static_cast<float>(std::clamp(state.pwm[a],-pid[a].limit,pid[a].limit));
                continue;
            }
            state.reference[a]+=std::clamp(state.target[a]-state.reference[a],-speed_mm_s*dt,speed_mm_s*dt);
            const double error=(state.reference[a]-state.position[a])*state.scale[a];
            if(std::abs(error)>tracking_limit_mm*state.scale[a]) {fail(Error::tracking); return {};}
            const double output=pid[a].step(error,dt);
            if(!std::isfinite(output)) {fail(Error::numeric);return {};}
            result[a]=static_cast<float>(std::clamp(output,-pid[a].limit,pid[a].limit));
            if(!std::isfinite(result[a])) {fail(Error::numeric); return {};}
        }
        return result;
    }
private:
    uint32_t last_time_=0;
};
}

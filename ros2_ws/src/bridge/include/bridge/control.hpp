#pragma once
#include "spi.hpp"
#include "controllers.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

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

struct Program;
enum class Op { runner_load, runner_start, runner_stop, target, pwm, mode, axis, system, stop, zero, clear, clear_dsp, kp, ki, kd, limit, digital, analog, record, controller, profile };
// Preserve existing numeric fault codes in recorded CSVs.
enum class Error { none, busy, disabled_required, range, wrong_mode=5, position_unconfigured, not_ready=8, numeric=10, deadline, telemetry, dsp, realtime };
inline const char* error_text(Error e) {
    switch(e) {
    case Error::none: return "";
    case Error::busy: return "The runner owns this channel or another operation is pending";
    case Error::disabled_required: return "Disable this axis before changing mode, controller type, zero, or duty limit";
    case Error::range: return "Value is invalid or outside the protocol/controller output range";
    case Error::wrong_mode: return "Command does not match the selected control mode";
    case Error::position_unconfigured: return "Position control is not configured for this channel";
    case Error::not_ready: return "Stop and clear the host fault before enabling";
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
    uint64_t order=0;
    bool input=false;
    const Program* program=nullptr;
    uint64_t record=0,stop_count=0;
    bool check_stop=false;
};
struct Sample {
    uint64_t stop_count=0,program_id=0,runner_rows=0,runner_row=0,runner_cycles=0;
    uint8_t runner_state=0,runner_completion=0;
    uint16_t runner_axes=0,runner_digital=0,runner_analog=0;
    Error runner_error=Error::none;
    Telemetry telemetry{};
    uint64_t number=0, timestamp=0, command_id=0, record_id=0;
    std::array<double,6> target{}, reference{}, position{};
    std::array<double,6> pwm{}, scale{counts_per_mm,counts_per_mm}, minimum{-50,-50}, maximum{50,50};
    std::array<uint8_t,6> mode{0,0,1,1,1,1}; // 0: position, 1: direct normalized DSP duty.
    std::array<int32_t,6> zero{};
    std::array<bool,6> selected{};
    std::array<bool,6> position_configured{true,true,false,false,false,false};
    std::array<Tuning,6> tuning{};
    std::array<ControllerConfig,6> controllers{};
    std::array<ProfileId,6> profiles{};
    std::array<uint64_t,6> controller_revision{};
    uint16_t digital_out=0;
    std::array<uint16_t,2> analog_out{};
    bool system=false, connected=false;
    Error fault=Error::none, command_error=Error::none;
    uint64_t deadlines=0, errors=0, dropped=0;
    uint64_t input_rejections=0, feedback_received_ns=0, telemetry_count=0, dsp_restarts=0;
    Error input_error=Error::none;
    unsigned input_channel=0;
    Op input_operation=Op::target;
    double max_cycle_us=0, max_lateness_us=0;
};
inline bool valid_reference(const Sample& s,unsigned axis,double value) {
    // DSP reference telemetry is a signed 32-bit encoder count, not a travel limit.
    const double counts=s.zero[axis]+value*s.scale[axis];
    return std::isfinite(value)&&std::isfinite(counts)&&
        counts>=std::numeric_limits<int32_t>::min()&&counts<=std::numeric_limits<int32_t>::max();
}
class Control {
public:
    Sample state{};
    std::array<AxisController,6> controllers{};
    bool initialized=false, dsp_clear_requested=false;
    uint16_t mask() const {
        if (!state.system || state.fault!=Error::none || !state.connected) return 0;
        uint16_t selected=0;
        for(unsigned a=0;a<kAxes;a++)if(state.selected[a])selected|=1U<<a;
        return selected;
    }
    bool enabled(unsigned axis) const { return (mask() & (1U<<axis))!=0; }
    void dsp_restarted() {
        fail(Error::telemetry);initialized=false;dsp_clear_requested=false;
        state.record_id=0;++state.dsp_restarts;
    }
    void receive(const Telemetry& t,bool synchronized=true) {
        state.telemetry=t;
        state.feedback_received_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
        ++state.telemetry_count;
        if(!synchronized){state.connected=false;return;}
        if(t.fault && !dsp_clear_requested)fail(Error::dsp);
        state.connected=true;
        if (!initialized) {
            for(unsigned a=0;a<kAxes;a++) state.zero[a]=t.position[a];
            state.timestamp=t.timestamp_us;
            initialized=true;
        } else state.timestamp+=static_cast<uint32_t>(t.timestamp_us-last_time_);
        last_time_=t.timestamp_us;
        for(unsigned a=0;a<kAxes;a++) {
            state.position[a]=state.position_configured[a] ?
                (static_cast<double>(t.position[a])-state.zero[a])/state.scale[a] : 0;
            state.tuning[a]=controllers[a].tuning();
            state.controllers[a]=controllers[a].config;
        }
        state.number++;
    }
    void fail(Error error) {
        state.fault=error; state.system=false; state.selected={};
        state.pwm={};state.digital_out=0;state.analog_out={0,0};
        for(auto& p:controllers) p.reset();
    }
    void deadline_missed(uint64_t missed,bool disarm) {
        state.deadlines+=missed;
        if(missed && disarm && state.system)fail(Error::deadline);
    }
    void apply(const Command& c) {
        const auto previous_error=state.command_error;
        state.command_error=Error::none;
        apply_command(c);
        if(c.input) {
            if(state.command_error!=Error::none) {
                ++state.input_rejections;state.input_error=state.command_error;
                state.input_channel=c.axis;state.input_operation=c.op;
            }
            state.command_error=previous_error;
        } else if(c.id) state.command_id=c.id;
    }
private:
    void apply_command(const Command& c) {
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
        if (a>=kAxes) { state.command_error=Error::range; return; }
        switch(c.op) {
        case Op::stop: ++state.stop_count;state.system=false; state.selected={}; state.pwm={}; state.digital_out=0;state.analog_out={0,0};break;
        case Op::clear:
            if (mask()!=0) { state.command_error=Error::disabled_required; return; }
            if(state.telemetry.fault) {state.command_error=Error::dsp;return;}
            state.fault=Error::none; state.system=false; state.selected={}; break;
        case Op::clear_dsp:
            if(state.system || state.telemetry.arm_mask) {state.command_error=Error::disabled_required;return;}
            if(!state.connected) {state.command_error=Error::not_ready;return;}
            dsp_clear_requested=true;break;
        case Op::system:
        case Op::axis:
            if(c.enabled && (!state.connected || state.fault!=Error::none || dsp_clear_requested)) {
                state.command_error=Error::not_ready; return;
            }
            if(c.op==Op::system){state.system=c.enabled;if(!c.enabled){state.digital_out=0;state.analog_out={0,0};}}else state.selected[a]=c.enabled;
            break;
        case Op::target:
            if(!state.position_configured[a]) {state.command_error=Error::position_unconfigured;return;}
            if(state.mode[a]!=0) {state.command_error=Error::wrong_mode; return;}
            if(!valid_reference(state,a,c.value)) {state.command_error=Error::range; return;}
            state.target[a]=c.value; break;
        case Op::pwm:
            if(state.mode[a]!=1) {state.command_error=Error::wrong_mode; return;}
            if(!std::isfinite(c.value) || std::abs(c.value)>controllers[a].limit) {state.command_error=Error::range; return;}
            state.pwm[a]=c.value; break;
        case Op::mode:
            if(enabled(a)) {state.command_error=Error::disabled_required; return;}
            if(c.value!=0 && c.value!=1) {state.command_error=Error::range; return;}
            if(c.value==0 && !state.position_configured[a]) {state.command_error=Error::position_unconfigured;return;}
            state.mode[a]=static_cast<uint8_t>(c.value);state.pwm[a]=0;
            state.target[a]=state.position[a];
            state.reference[a]=state.position[a];controllers[a].reset();break;
        case Op::zero:
            if(!state.position_configured[a]) {state.command_error=Error::position_unconfigured;return;}
            if(enabled(a)) {state.command_error=Error::disabled_required; return;}
            if(!state.connected) {state.command_error=Error::not_ready; return;}
            state.zero[a]=state.telemetry.position[a];
            state.position[a]=state.target[a]=state.reference[a]=0;
            state.pwm[a]=0;
            controllers[a].reset(); break;
        case Op::record: state.record_id=static_cast<uint64_t>(c.value); break;
        case Op::controller:
            if(!state.position_configured[a]) {state.command_error=Error::position_unconfigured;return;}
            if(enabled(a)&&(c.controller.type!=controllers[a].config.type || c.controller.slew_enabled!=controllers[a].config.slew_enabled)) {state.command_error=Error::disabled_required;return;}
            if(!valid_controller(c.controller)) {state.command_error=Error::range;return;}
            controllers[a].configure(c.controller,enabled(a));
            if(c.profile[0])state.profiles[a]=c.profile;
            state.controller_revision[a]++;break;
        case Op::profile:
            state.profiles[a]=c.profile;state.controller_revision[a]++;break;
        default:
            if(c.op==Op::limit && enabled(a)) {state.command_error=Error::disabled_required; return;}
            if(!std::isfinite(c.value) || c.value<0 || (c.op==Op::limit && (c.value<=0 || c.value>1))) {
                state.command_error=Error::range; return;
            }
            if(c.op==Op::limit) {controllers[a].limit=c.value;controllers[a].reset();break;}
            if(controllers[a].config.type!=0) {state.command_error=Error::wrong_mode;return;}
            auto candidate=controllers[a].config;
            candidate.parameters[c.op==Op::kp?0:c.op==Op::ki?1:2]=c.value;
            if(!valid_controller(candidate)) {state.command_error=Error::range;return;}
            controllers[a].configure(candidate,enabled(a));state.controller_revision[a]++;break;
        }
        const auto after=mask();
        for(unsigned i=0;i<kAxes;i++) {
            if((before^after)&(1U<<i)) {
                controllers[i].reset(); state.reference[i]=state.position[i];
                state.target[i]=state.position[i];
                state.pwm[i]=0;
            }
        }
    }
public:
    Values duty() {
        Values result{};
        for(unsigned a=0;a<kAxes;a++) {
            if(!enabled(a)) {controllers[a].reset(); continue;}
            if(state.mode[a]==1) {
                result[a]=static_cast<float>(std::clamp(state.pwm[a],-controllers[a].limit,controllers[a].limit));
                continue;
            }
            const double output=controllers[a].step(state.target[a],state.position[a],state.scale[a],dt);
            state.reference[a]=controllers[a].reference_mm;
            if(!std::isfinite(output)) {fail(Error::numeric);return {};}
            result[a]=static_cast<float>(std::clamp(output,-controllers[a].limit,controllers[a].limit));
            if(!std::isfinite(result[a])) {fail(Error::numeric); return {};}
        }
        return result;
    }
private:
    uint32_t last_time_=0;
};
}

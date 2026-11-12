#include "bridge/control.hpp"
#include "bridge/diagnostics.hpp"
#include <sstream>
#include <cassert>
#include <iostream>
#include <thread>
using namespace diamond;
int main() {
    AxisController unshaped;unshaped.step(10,0,1,dt);assert(unshaped.reference_mm==10);
    unshaped.config.slew_enabled=true;unshaped.reset();unshaped.step(10,0,1,dt);
    assert(unshaped.reference_mm==20*dt);
    // Large tracking errors remain valid controller inputs, including a stationary plant.
    Control research;research.receive({});
    research.apply({Op::axis,0,0,true});research.apply({Op::axis,1,0,true});research.apply({Op::system,0,0,true});
    for(const double target:{20.0,-20.0,50.0,-50.0,75.0,-75.0}) {
        research.apply({Op::target,0,target});research.apply({Op::target,1,-target});
        const auto output=research.duty();
        assert(research.state.fault==Error::none&&research.mask()==3);
        assert(research.state.reference[0]==target&&research.state.reference[1]==-target);
        for(unsigned a=0;a<2;a++)assert(std::isfinite(output[a])&&std::abs(output[a])<=research.controllers[a].limit);
    }
    for(unsigned tick=0;tick<20000;tick++) {
        const auto output=research.duty();
        assert(output[0]==1&&output[1]==-1&&research.mask()==3&&research.state.fault==Error::none);
    }
    research.apply({Op::stop});assert(research.mask()==0);
    Control timing;timing.receive({});timing.apply({Op::axis,0,0,true});timing.apply({Op::system,0,0,true});
    timing.apply({Op::target,0,75});timing.apply({Op::digital,0,1});
    timing.deadline_missed(3,false);
    assert(timing.state.deadlines==3&&timing.mask()==1&&timing.state.target[0]==75&&timing.state.digital_out==1);
    timing.deadline_missed(0,true);assert(timing.mask()==1);
    timing.deadline_missed(2,true);
    assert(timing.state.deadlines==5&&timing.mask()==0&&timing.state.fault==Error::deadline&&!timing.state.digital_out);
    timing.apply({Op::clear});timing.apply({Op::system,0,0,true});timing.apply({Op::digital,0,1});
    timing.deadline_missed(1,true);assert(!timing.state.system&&!timing.state.digital_out); // I/O-only operation also stops.
    const auto origin=Clock::time_point{};
    assert(missed_periods(origin,origin+control_period)==0);
    assert(missed_periods(origin,origin+control_period+Clock::duration{1})==1);
    assert(missed_periods(origin,origin+2*control_period)==1);
    assert(missed_periods(origin,origin+2*control_period+Clock::duration{1})==2);
    assert(missed_periods(origin,origin+std::chrono::microseconds(2300))==4);
    CycleHistory history;RealtimeEvent event;
    history.copy(event);assert(event.count==0);
    history.push({1});history.copy(event);assert(event.count==1&&event.history[0].number==1);
    for(unsigned n=2;n<=11;++n)history.push({n});
    history.copy(event);assert(event.count==8);
    for(unsigned i=0;i<8;++i)assert(event.history[i].number==i+4);
    std::strcpy(event.error.data(),"Stale DSP telemetry");event.spi.result=192;event.spi.rx[95]=1234;
    event.deadline=true;event.missed_periods=4;
    std::ostringstream log;write_event(log,event);
    assert(log.str().find("\"error\":\"Stale DSP telemetry\"")!=std::string::npos);
    assert(log.str().find("\"ioctl_result\":192")!=std::string::npos);
    assert(log.str().find("\"missed_periods\":4,\"period_us\":500")!=std::string::npos);
    assert(log.str().find(",1234]}\n")!=std::string::npos);
    Control c;c.controllers[0].config.slew_enabled=true;Telemetry sample{};sample.position[0]=12345;sample.position[1]=-500;sample.timestamp_us=0xfffffff0;
    c.receive(sample);
    assert(c.state.position[0]==0&&c.state.zero[0]==12345);
    assert(std::abs(counts_per_mm-321.846662696944)<1e-9);
    c.apply({Op::axis,0,0,true,1});c.apply({Op::system,0,0,true,2});
    assert(c.mask()==1);
    c.apply({Op::target,0,50,false,3});
    assert(c.state.target[0]==50);
    const auto u=c.duty();assert(u[0]<0 && std::abs(c.state.reference[0]-20*dt)<1e-12);
    c.apply({Op::zero,0,0,false,4});assert(c.state.command_error==Error::disabled_required);
    assert(c.state.zero[0]==12345);
    c.apply({Op::target,0,75,false,5});assert(c.state.command_error==Error::none&&c.state.target[0]==75);
    c.apply({Op::target,0,1e300});assert(c.state.command_error==Error::range&&c.state.target[0]==75);
    c.apply({Op::target,0,NAN,false,6});assert(c.state.command_error==Error::range);
    c.apply({Op::axis,0,0,false,7});assert(c.mask()==0);
    sample.position[0]+=1000;sample.timestamp_us=984;c.receive(sample);
    assert(c.state.timestamp==static_cast<uint64_t>(0xfffffff0)+1000);
    c.apply({Op::zero,0,0,false,8});
    assert(c.state.zero[0]==13345&&c.state.position[0]==0&&c.state.target[0]==0&&c.state.reference[0]==0);
    c.apply({Op::axis,0,0,true,9});assert(c.state.zero[0]==13345);
    c.apply({Op::stop,0,0,false,10});assert(c.mask()==0&&!c.state.selected[0]&&!c.state.system);
    c.fail(Error::telemetry);c.apply({Op::system,0,0,true,11});assert(c.mask()==0&&c.state.command_error==Error::not_ready);
    c.apply({Op::clear,0,0,false,12});c.dsp_clear_requested=false;assert(c.state.fault==Error::none&&!c.state.system);
    c.apply({Op::record,0,1,false,13});assert(c.state.record_id==1);
    sample.position[0]+=20000;sample.timestamp_us+=1000;c.receive(sample);
    c.apply({Op::axis,0,0,true,14});c.apply({Op::system,0,0,true,15});
    assert(c.mask()==1&&c.state.command_error==Error::none&&c.state.target[0]==c.state.position[0]);
    c.apply({Op::axis,0,0,false});
    c.apply({Op::zero,0,0,false,16});c.apply({Op::axis,0,0,true,17});assert(c.mask()==1);
    c.apply({Op::mode,0,1,false,18});assert(c.state.command_error==Error::disabled_required);
    c.apply({Op::stop});c.apply({Op::mode,0,1});assert(c.state.mode[0]==1&&c.state.pwm[0]==0);
    c.apply({Op::target,0,1});assert(c.state.command_error==Error::wrong_mode);
    c.apply({Op::limit,0,0.25});c.apply({Op::pwm,0,0.3});assert(c.state.command_error==Error::range);
    c.apply({Op::pwm,0,NAN});assert(c.state.command_error==Error::range);
    c.apply({Op::pwm,0,0.2});c.apply({Op::axis,0,0,true});c.apply({Op::system,0,0,true});
    assert(c.state.pwm[0]==0&&c.duty()[0]==0);
    c.apply({Op::pwm,0,0.2});assert(std::abs(c.duty()[0]-0.2)<1e-7);
    c.apply({Op::pwm,0,-0.2});assert(std::abs(c.duty()[0]+0.2)<1e-7);
    c.state.position[0]=75;assert(std::abs(c.duty()[0]+.2)<1e-7);
    assert(c.state.fault==Error::none&&c.mask()==1&&c.state.pwm[0]==-.2);
    c.apply({Op::stop});c.apply({Op::mode,0,0});assert(c.state.mode[0]==0&&c.state.target[0]==75);
    Control io;io.receive({});
    io.apply({Op::digital,3,1});assert(io.state.command_error==Error::not_ready);
    io.apply({Op::system,0,0,true});assert(io.mask()==0&&io.state.system);
    io.apply({Op::digital,3,1});io.apply({Op::analog,1,4095});
    assert(io.state.digital_out==8&&io.state.analog_out[1]==4095);
    io.apply({Op::analog,1,4096});assert(io.state.command_error==Error::range&&io.state.analog_out[1]==4095);
    io.apply({Op::stop});assert(!io.state.digital_out&&!io.state.analog_out[1]);
    Control calibrated;calibrated.state.scale[0]=100;
    calibrated.receive({});sample.position[0]=100;sample.timestamp_us=1000;calibrated.receive(sample);
    assert(calibrated.state.position[0]==1);
    PidController live;live.tuning={0.01,0,0,1,1};
    assert(std::abs(live.step(10,dt)-0.1)<1e-12);
    auto changed=live.tuning;changed.kp=0.02;changed.ki=0.1;
    live.retune(changed,true);
    assert(std::abs(live.step(10,dt)-0.1)<1e-12); // No output jump on the gain update.
    assert(live.step(10,dt)>0.1); // New Ki takes effect without resetting state.
    Control online;online.receive({});online.apply({Op::axis,0,0,true});online.apply({Op::system,0,0,true});
    online.state.profiles[0]=profile_id("shared");
    Command tune;tune.op=Op::controller;tune.controller.parameters={0.0006,0.002,0.00001};
    online.apply(tune);assert(online.mask()==1&&online.state.command_error==Error::none);
    assert(online.controllers[0].config.parameters==tune.controller.parameters&&online.controllers[1].config.parameters[0]==0.0005);
    assert(std::string(online.state.profiles[0].data())=="shared");
    tune.controller.type=1;online.apply(tune);assert(online.state.command_error==Error::disabled_required);
    tune.controller.type=0;tune.controller.parameters[1]=NAN;online.apply(tune);
    assert(online.state.command_error==Error::range&&online.controllers[0].config.parameters[1]==0.002);
    online.apply({Op::kp,0,0.0007});assert(online.state.command_error==Error::none&&online.mask()==1);
    online.apply({Op::limit,0,0.2});assert(online.state.command_error==Error::disabled_required);
    Queue<unsigned,64> queue;
    std::thread producer([&]{for(unsigned i=0;i<100000;i++)while(!queue.push(i))std::this_thread::yield();});
    for(unsigned i=0;i<100000;i++){unsigned value=0;while(!queue.pop(value))std::this_thread::yield();assert(value==i);}
    producer.join();
    std::cout<<"PASS: calibration, zero interlocks, targets, enable/fault transitions, timestamp wrap, SPSC ordering\n";
}

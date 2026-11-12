#include "bridge/inputs.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using namespace diamond;
int main() {
    Inputs inputs;
    Command a{Op::pwm,5,.2};a.order=1;inputs.publish(a);
    a.value=.3;a.order=2;inputs.publish(a);
    Command b{Op::target,0,4};b.order=3;inputs.publish(b);
    std::array<Command,Inputs::count> got;
    const auto count=inputs.take(got.data());assert(count==2);
    std::sort(got.begin(),got.begin()+count,[](const auto& x,const auto& y){return x.order<y.order;});
    assert(got[0].axis==5&&got[0].value==.3&&got[0].input);
    assert(inputs.take(got.data())==0); // Consumed inputs must not be replayed after stop/enable.
    Control core;Telemetry t{};t.encoder_valid=63;core.receive(t);
    core.apply({Op::axis,5,0,true});core.apply({Op::system,0,0,true});
    core.apply(got[0]);assert(core.mask()==32);
    for(unsigned i=0;i<10000;i++)assert(std::abs(core.duty()[5]-.3)<1e-6); // Publisher silence holds duty.
    core.apply({Op::stop});assert(core.duty()[5]==0);
    core.apply({Op::axis,5,0,true});core.apply({Op::system,0,0,true});assert(core.duty()[5]==0);
    assert(!core.state.position_configured[5]);
    core.apply({Op::mode,5,0});assert(core.state.command_error==Error::disabled_required);
    const auto discrete_error=core.state.command_error;
    a.value=2;core.apply(Command{Op::pwm,5,2,false,0,{}, {},4,true});
    assert(core.state.input_rejections==1&&core.state.input_error==Error::range);
    assert(core.state.command_error==discrete_error); // Topic rejection cannot overwrite a service result.
    core.apply({Op::stop});core.apply({Op::mode,5,0});assert(core.state.command_error==Error::not_ready);
    t.fault=PCD_FAULT_TIMEOUT;core.receive(t);core.apply({Op::clear});
    assert(core.state.fault==Error::dsp&&core.state.command_error==Error::dsp);
    core.apply({Op::clear_dsp});assert(core.dsp_clear_requested&&core.state.fault==Error::dsp);
    t.fault=0;core.dsp_clear_requested=false;core.receive(t);core.apply({Op::clear});
    assert(core.state.fault==Error::none&&!core.state.system);

    struct Pair {uint64_t value=0,inverse=~uint64_t{0};};
    Latest<Pair> mailbox;std::atomic<bool> done=false;
    std::thread producer([&]{for(uint64_t i=1;i<=200000;i++)mailbox.publish({i,~i});done=true;});
    Pair pair;uint64_t last=0;
    while(!done.load()||last!=200000)if(mailbox.take(pair)) {
        assert(pair.value>last&&pair.inverse==~pair.value);last=pair.value;
    }
    producer.join();

    SpiSession session;Telemetry frame{};frame.session=session.id;
    frame.sequence=100;frame.timestamp_us=1000;frame.command_sequence=1;
    session.accept(frame);assert(session.ready);
    bool rejected=false;try{session.accept(frame);}catch(const std::exception&){rejected=true;}assert(rejected);
    frame={};frame.sequence=1;frame.timestamp_us=200;
    session.accept(frame);assert(!session.ready&&session.rebooted&&session.changed);
    session.accept(frame);assert(!session.rebooted); // Count one DSP reboot, even if SYNC takes multiple frames.
    frame.session=session.id;frame.command_sequence=1;frame.sequence=2;frame.timestamp_us=400;
    session.accept(frame);assert(session.ready&&!session.rebooted);
    // A new host session does not reset DSP counters or imply a DSP reboot.
    session.resynchronize();frame.session=123;session.accept(frame);assert(!session.ready&&!session.rebooted);
    frame.session=session.id;session.accept(frame);assert(session.ready);
    // Counter wrap is still accepted; stale data within a session still is not.
    SpiSession wrap;frame.session=wrap.id;frame.sequence=0xfffffff0;frame.timestamp_us=0xfffffff0;
    wrap.accept(frame);frame.sequence=2;frame.timestamp_us=200;wrap.accept(frame);assert(wrap.ready);
    core.state.record_id=7;core.dsp_restarted();assert(!core.initialized&&!core.state.record_id&&!core.mask());
    t.position[0]=500;core.receive(t);assert(core.state.zero[0]==500&&core.state.position[0]==0);
    std::cout<<"PASS: latest inputs, hold, source gate, separate fault clears, concurrent mailbox and DSP resynchronization\n";
}

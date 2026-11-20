#include "bridge/runner.hpp"
#include "bridge/recorder.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
using namespace diamond;
int main(){
    // Echo matching is independent of controller type, transfer delay and wrap.
    ControlHistory history;ControlFrame first_tick{},second_tick{};
    first_tick.host_tick=11;first_tick.digital_out=9;second_tick.host_tick=12;
    history.put(7,UINT32_MAX,first_tick);history.put(7,0,second_tick);
    assert(history.find(7,UINT32_MAX).digital_out==9 && history.find(7,0).host_tick==12);
    assert(history.find(7,UINT32_MAX).host_tick==11 && !history.find(8,UINT32_MAX).host_tick);
    history.put(7,64,first_tick);
    assert(!history.find(7,0).host_tick && !history.find(0,0).host_tick);
    Control core;core.receive({});
    auto mixed=core.state;mixed.unit[1]=PositionUnit::deg;
    Program mixed_program;mixed_program.name="linear-and-rotary";mixed_program.channels={0,19};mixed_program.values={1.25,-12.5};
    validate_program(mixed_program,mixed);assert(mixed_program.channels[1]==1&&mixed_program.axes==3);
    Program wrong;wrong.name="bad unit";wrong.channels={1};wrong.values={2};
    bool wrong_unit=false;try{validate_program(wrong,mixed);}catch(const std::runtime_error&){wrong_unit=true;}assert(wrong_unit);
    const auto catalog=signal_catalog(mixed.unit);
    assert(std::any_of(catalog.begin(),catalog.end(),[](const Signal& f){return f.name=="axis1_ctrl_r_deg"&&f.unit=="deg";}));
    assert(std::none_of(catalog.begin(),catalog.end(),[](const Signal& f){return f.name=="axis1_position_mm";}));
    Program p;p.id=1;p.name="pulse";p.channels={0,1,12};
    for(int i=0;i<40;i++){p.values.push_back(i*.01);p.values.push_back(-i*.01);p.values.push_back(i<10?1:0);}
    validate_program(p,core.state);
    Program wide;wide.name="outside-slider";wide.channels={0,1};wide.values={75,-75};
    validate_program(wide,core.state);
    assert(valid_value(0,75,core.state)&&!valid_value(0,1e300,core.state));
    Runner runner;Command load{};load.op=Op::runner_load;load.program=&p;
    assert(runner.command(load,core)==Error::none);
    Command start{};start.op=Op::runner_start;start.value=2;start.enabled=true;start.record=17;
    assert(runner.command(start,core)==Error::none&&core.mask()==3&&core.state.record_id==17);
    for(int i=0;i<100;i++){
        runner.tick(core);assert(core.state.runner_row==unsigned(i%40));
        assert(core.state.digital_out==(i%40<10?1:0));assert(core.state.target[0]==(i%40)*.01);
        assert(core.state.target[1]==-(i%40)*.01);
    }
    assert(runner.conflicts({Op::target,0,1},core.state));
    assert(!runner.conflicts({Op::digital,3,1},core.state));
    core.state.digital_out|=8;runner.stop(core);
    assert(!core.mask()&&core.state.digital_out==8&&!core.state.record_id);
    start.value=0;assert(runner.command(start,core)==Error::none);
    for(int i=0;i<41;i++)runner.tick(core);
    assert(core.state.runner_state==2&&core.state.target[0]==.39&&core.state.record_id==17);
    runner.stop(core);start.value=1;assert(runner.command(start,core)==Error::none);
    for(int i=0;i<41;i++)runner.tick(core);
    assert(core.state.runner_state==3&&!core.mask()&&!core.state.record_id);
    start.value=2;assert(runner.command(start,core)==Error::none);
    core.fail(Error::telemetry);runner.tick(core);assert(core.state.runner_state==4&&!core.state.record_id);
    core.apply({Op::clear});
    Program bad=p;bad.axes=bad.digital=bad.analog=0;bad.channels={0,6};bad.values={0,.1};
    bool rejected=false;try{validate_program(bad,core.state);}catch(...){rejected=true;}assert(rejected);
    start.check_stop=true;start.stop_count=core.state.stop_count;core.apply({Op::stop});
    assert(runner.command(start,core)==Error::not_ready&&!core.mask());
    const auto dir=std::filesystem::temp_directory_path()/("pcd-data-"+std::to_string(getpid()));std::filesystem::create_directories(dir);
    Recorder recorder(dir);recorder.start(1,{"axis0_position_mm","do0_command"},core.state,"test");
    auto sample=core.state;sample.connected=true;sample.record_id=1;
    for(int i=1;i<=10;i++){sample.number=i;sample.timestamp=i*500;recorder.write(sample);}
    sample.record_id=0;recorder.write(sample);
    assert(!recorder.active()&&recorder.samples==10&&recorder.filename.find("session-")!=0);
    std::ifstream file(dir/recorder.filename);std::string line;unsigned rows=0;
    while(std::getline(file,line))if(line[0]!='#'){if(rows==0)assert(line=="timestamp_us,sample,axis0_position_mm,do0_command");rows++;}
    assert(rows==11);const auto first=recorder.filename;recorder.start(2,{},sample,"test");assert(recorder.filename!=first);recorder.finish();
    // Stop can be issued before the DSP echoes the last two excitation commands.
    recorder.start(7,{"step_tick"},sample,"test",80);
    sample.record_id=7;sample.command_id=80;sample.host_tick=11;sample.number++;
    sample.acknowledged={};recorder.write(sample);
    sample.host_tick++;sample.number++;sample.acknowledged.record=7;sample.acknowledged.host_tick=11;sample.acknowledged.experiment_tick=0;
    recorder.write(sample);sample.host_tick++;sample.number++;sample.record_id=0;
    sample.acknowledged.host_tick=12;sample.acknowledged.experiment_tick=1;recorder.write(sample);
    assert(recorder.active() && recorder.samples==2);
    sample.acknowledged.record=0;sample.acknowledged.host_tick=13;recorder.write(sample);
    assert(!recorder.active());
    std::filesystem::remove_all(dir);
    std::cout<<"PASS: atomic rows, 10-tick pulse, loop/hold/stop, channel ownership, fault recording stop, selected CSV columns\n";
}

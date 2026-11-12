#include "bridge/runner.hpp"
#include "bridge/recorder.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
using namespace diamond;
int main(){
    Control core;core.receive({});
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
    std::filesystem::remove_all(dir);
    std::cout<<"PASS: atomic rows, 10-tick pulse, loop/hold/stop, channel ownership, fault recording stop, selected CSV columns\n";
}

#pragma once
#include "control.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <vector>

namespace diamond {
struct Signal {
    std::string name,unit,group;
    std::function<double(const Sample&)> number;
    std::function<std::string(const Sample&)> text;
};
inline std::vector<Signal> signal_catalog() {
    std::vector<Signal> out;
    auto add=[&](std::string name,std::string unit,std::string group,auto fn){out.push_back({name,unit,group,fn,{}});};
    for(unsigned a=0;a<6;a++) {
        const auto prefix="axis"+std::to_string(a)+"_",group="Axis "+std::to_string(a);
        add(prefix+"target_mm","mm",group,[a](const Sample& s){return s.target[a];});
        add(prefix+"reference_mm","mm",group,[a](const Sample& s){return s.reference[a];});
        add(prefix+"position_mm","mm",group,[a](const Sample& s){return s.position[a];});
        add(prefix+"raw_counts","count",group,[a](const Sample& s){return s.telemetry.position[a];});
        add(prefix+"zero_counts","count",group,[a](const Sample& s){return s.zero[a];});
        add(prefix+"requested_duty","duty",group,[a](const Sample& s){return s.telemetry.requested_duty[a];});
        add(prefix+"output_duty","duty",group,[a](const Sample& s){return s.telemetry.duty[a];});
        add(prefix+"enabled","bool",group,[a](const Sample& s){return s.system&&s.selected[a]&&s.fault==Error::none;});
        add(prefix+"position_configured","bool",group,[a](const Sample& s){return s.position_configured[a];});
        add(prefix+"kp","duty/count",group,[a](const Sample& s){return s.controllers[a].parameters[0];});
        add(prefix+"ki","duty/(count*s)",group,[a](const Sample& s){return s.controllers[a].parameters[1];});
        add(prefix+"kd","duty*s/count",group,[a](const Sample& s){return s.controllers[a].parameters[2];});
        add(prefix+"d_filter_hz","Hz",group,[a](const Sample& s){return s.controllers[a].parameters[3];});
        add(prefix+"duty_limit","duty",group,[a](const Sample& s){return s.tuning[a].limit;});
        add(prefix+"mode","enum",group,[a](const Sample& s){return s.mode[a];});
        add(prefix+"pwm_target","duty",group,[a](const Sample& s){return s.pwm[a];});
        add(prefix+"slew_enabled","bool",group,[a](const Sample& s){return s.controllers[a].slew_enabled;});
        add(prefix+"slew_rate_mm_s","mm/s",group,[a](const Sample& s){return s.controllers[a].slew_rate_mm_s;});
        out.push_back({prefix+"controller_type","text",group,{},[a](const Sample& s){return std::string(controller_types[s.controllers[a].type].id);}});
        out.push_back({prefix+"controller_profile","text",group,{},[a](const Sample& s){return std::string(s.profiles[a].data());}});
        out.push_back({prefix+"controller_parameters","text",group,{},[a](const Sample& s){
            std::ostringstream v;v<<std::setprecision(17);const auto& c=s.controllers[a];const auto& type=controller_types[c.type];
            for(unsigned i=0;i<type.count;i++)v<<(i?";":"")<<type.parameters[i].name<<'='<<c.parameters[i];
            return v.str();}});
        add("encoder"+std::to_string(a)+"_counts","count","DSP",[a](const Sample& s){return s.telemetry.position[a];});
        add("dsp_reference"+std::to_string(a)+"_counts","count","DSP",[a](const Sample& s){return s.telemetry.reference[a];});
        add("ai"+std::to_string(a)+"_raw","code","Analog I/O",[a](const Sample& s){return s.telemetry.analog_in[a];});
    }
    for(unsigned i=0;i<4;i++) {
        add("di"+std::to_string(i),"bool","Digital I/O",[i](const Sample& s){return (s.telemetry.digital_in>>i)&1;});
        add("do"+std::to_string(i),"bool","Digital I/O",[i](const Sample& s){return (s.telemetry.digital_out>>i)&1;});
        add("do"+std::to_string(i)+"_command","bool","Digital I/O",[i](const Sample& s){return (s.digital_out>>i)&1;});
    }
    for(unsigned i=0;i<2;i++) {
        add("ao"+std::to_string(i)+"_raw","code","Analog I/O",[i](const Sample& s){return s.telemetry.analog_out[i];});
        add("ao"+std::to_string(i)+"_command","code","Analog I/O",[i](const Sample& s){return s.analog_out[i];});
    }
#define PCD_SIGNAL(name,unit,group,expression) add(name,unit,group,[](const Sample& s){return expression;})
    PCD_SIGNAL("dsp_timestamp_us","us","DSP",s.telemetry.timestamp_us);
    PCD_SIGNAL("dsp_sequence","count","DSP",s.telemetry.sequence);
    PCD_SIGNAL("dsp_session","id","DSP",s.telemetry.session);
    PCD_SIGNAL("dsp_command_sequence","count","DSP",s.telemetry.command_sequence);
    PCD_SIGNAL("dsp_state","enum","DSP",s.telemetry.state);
    PCD_SIGNAL("dsp_arm_mask","mask","DSP",s.telemetry.arm_mask);
    PCD_SIGNAL("dsp_fault","mask","DSP",s.telemetry.fault);
    PCD_SIGNAL("digital_in","mask","Digital I/O",s.telemetry.digital_in);
    PCD_SIGNAL("digital_out","mask","Digital I/O",s.telemetry.digital_out);
    PCD_SIGNAL("analog_valid","mask","Analog I/O",s.telemetry.analog_valid);
    PCD_SIGNAL("analog_out_valid","mask","Analog I/O",s.telemetry.analog_out_valid);
    PCD_SIGNAL("encoder_valid","mask","DSP",s.telemetry.encoder_valid);
    PCD_SIGNAL("dsp_invalid_frames","count","DSP",s.telemetry.invalid_frames);
    PCD_SIGNAL("dsp_rejected_commands","count","DSP",s.telemetry.rejected_commands);
    PCD_SIGNAL("dsp_command_timeouts","count","DSP",s.telemetry.watchdogs);
    PCD_SIGNAL("dsp_encoder_errors","count","DSP",s.telemetry.encoder_errors);
    PCD_SIGNAL("cpu2_max_poll_us","us","DSP",s.telemetry.cpu2_max_poll_us);
    PCD_SIGNAL("system_enabled","bool","Host",s.system);
    PCD_SIGNAL("host_fault","enum","Host",static_cast<unsigned>(s.fault));
    PCD_SIGNAL("deadline_misses","count","Host",s.deadlines);
    PCD_SIGNAL("telemetry_errors","count","Host",s.errors);
    PCD_SIGNAL("dropped_samples","count","Host",s.dropped);
    PCD_SIGNAL("input_rejections","count","Host",s.input_rejections);
    PCD_SIGNAL("max_cycle_us","us","Host",s.max_cycle_us);
    PCD_SIGNAL("max_lateness_us","us","Host",s.max_lateness_us);
    PCD_SIGNAL("dsp_restarts","count","Host",s.dsp_restarts);
    PCD_SIGNAL("runner_state","enum","Runner",s.runner_state);
    PCD_SIGNAL("runner_row","tick","Runner",s.runner_row);
    PCD_SIGNAL("runner_cycles","count","Runner",s.runner_cycles);
#undef PCD_SIGNAL
    return out;
}
class Recorder {
    std::filesystem::path directory_;
    std::ofstream file_;
    uint64_t id_=0,restarts_=0,last_=0;
    bool started_=false;
    std::vector<size_t> columns_;
public:
    const std::vector<Signal> catalog=signal_catalog();
    std::string filename,error;
    uint64_t samples=0;
    std::vector<std::string> selected;
    explicit Recorder(std::filesystem::path directory):directory_(std::move(directory)){}
    bool active() const {return file_.is_open();}
    uint64_t id() const {return id_;}
    void start(uint64_t id,const std::vector<std::string>& names,const Sample& s,const std::string& project) {
        if(active())throw std::runtime_error("A recording is already active");
        std::vector<size_t> columns;
        if(names.empty()){for(size_t i=0;i<catalog.size();i++)columns.push_back(i);}
        else for(const auto& name:names) {
            const auto it=std::find_if(catalog.begin(),catalog.end(),[&](const Signal& x){return x.name==name;});
            if(it==catalog.end())throw std::runtime_error("Unknown recording signal: "+name);
            const auto index=static_cast<size_t>(it-catalog.begin());
            if(std::find(columns.begin(),columns.end(),index)!=columns.end())throw std::runtime_error("Duplicate recording signal");
            columns.push_back(index);
        }
        const auto now=std::time(nullptr);std::tm local{};localtime_r(&now,&local);
        std::ostringstream base;base<<std::put_time(&local,"%Y-%m-%d_%H-%M-%S");filename=base.str()+".csv";
        for(unsigned n=1;std::filesystem::exists(directory_/filename);n++)filename=base.str()+"_"+std::to_string(n)+".csv";
        file_.clear();file_.open(directory_/filename);
        if(!file_)throw std::runtime_error("Cannot create recording");
        columns_=std::move(columns);selected.clear();for(auto i:columns_)selected.push_back(catalog[i].name);
        id_=id;restarts_=s.dsp_restarts;samples=0;last_=0;started_=false;error.clear();
        file_<<std::setprecision(17)<<"# project="<<project<<"; period_s="<<dt<<"; timezone="<<std::put_time(&local,"%z")<<"\n";
        for(unsigned a=0;a<6;a++)file_<<"# axis"<<a<<" counts_per_mm="<<s.scale[a]<<"; zero_counts="<<s.zero[a]
            <<"; min_mm="<<s.minimum[a]<<"; max_mm="<<s.maximum[a]<<"; profile="<<s.profiles[a].data()
            <<"; slew="<<s.controllers[a].slew_enabled<<"; rate_mm_s="<<s.controllers[a].slew_rate_mm_s<<'\n';
        file_<<"timestamp_us,sample";for(auto i:columns_)file_<<','<<catalog[i].name;file_<<'\n';
        if(!file_){finish();throw std::runtime_error("Cannot write recording header");}
    }
    void finish() {
        if(!active())return;
        file_.flush();if(!file_)error="Recording write failed (check disk space)";
        file_.close();file_.clear();
    }
    void write(const Sample& s) {
        if(!active())return;
        if(s.dsp_restarts!=restarts_){error="Recording ended because the DSP restarted; a new startup zero was established";finish();return;}
        if(s.record_id==id_)started_=true;
        else {if(started_)finish();return;}
        if(!s.connected||s.number==last_)return;
        last_=s.number;file_<<s.timestamp<<','<<s.number;
        for(auto i:columns_){const auto& field=catalog[i];file_<<',';if(field.number)file_<<field.number(s);else file_<<field.text(s);}
        file_<<'\n';++samples;
        if(!file_)finish();
    }
};
}

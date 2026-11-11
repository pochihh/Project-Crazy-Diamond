#include "bridge/control.hpp"
#include "bridge/profiles.hpp"
#include "bridge/diagnostics.hpp"
#include <rclcpp/rclcpp.hpp>
#include <interfaces/msg/bridge_state.hpp>
#include <interfaces/srv/command.hpp>
#include <interfaces/srv/controller_catalog.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>

using namespace diamond;
namespace fs=std::filesystem;

class Bridge final : public rclcpp::Node {
public:
    Bridge():Node("bridge") {
        if(declare_parameter("project_id",std::string(DIAMOND_PROJECT_ID))!=DIAMOND_PROJECT_ID)
            throw std::runtime_error("Project configuration does not match the compiled project ID");
        mock_=declare_parameter("mock",true);
        device_=declare_parameter("spi_device",std::string("/dev/spidev0.0"));
        directory_=declare_parameter("recording_directory",std::string("recordings"));
        core_.speed_mm_s=declare_parameter("speed_mm_s",20.0);
        core_.tracking_limit_mm=declare_parameter("tracking_limit_mm",15.0);
        profiles_=std::make_unique<Profiles>(declare_parameter("controller_directory",std::string("projects/")+DIAMOND_PROJECT_ID+"/controllers"));
        for(unsigned a=0;a<2;a++) {
            const auto key="axis"+std::to_string(a)+".";
            auto& controller=core_.pid[a];
            core_.state.scale[a]=declare_parameter(key+"counts_per_mm",counts_per_mm);
            core_.state.minimum[a]=declare_parameter(key+"minimum_mm",-50.0);
            core_.state.maximum[a]=declare_parameter(key+"maximum_mm",50.0);
            if(!std::isfinite(core_.state.scale[a]) || core_.state.scale[a]<=0 ||
               !std::isfinite(core_.state.minimum[a]) || !std::isfinite(core_.state.maximum[a]) ||
               core_.state.minimum[a]>=core_.state.maximum[a] || core_.state.minimum[a]>0 || core_.state.maximum[a]<0)
                throw std::runtime_error("Invalid axis calibration or travel range");
            controller.sign=declare_parameter(key+"motor_sign",-1.0);
            controller.limit=declare_parameter(key+"duty_limit",1.0);
            if((controller.sign!=1&&controller.sign!=-1)||!(controller.limit>0&&controller.limit<=1))
                throw std::runtime_error("Invalid axis tuning");
            const auto profile=profiles_->load(declare_parameter(key+"controller_profile",std::string("bench_pid")));
            controller.configure(profile.controller,false);
            core_.state.profiles[a]=profile.id;
        }
        if(!(core_.speed_mm_s>0&&core_.speed_mm_s<=100)||!(core_.tracking_limit_mm>0&&core_.tracking_limit_mm<=100))
            throw std::runtime_error("Invalid motion limits");
        fs::create_directories(directory_);
        diagnostics_.open(fs::path(directory_)/"realtime-events.jsonl",std::ios::app);
        if(!diagnostics_)throw std::runtime_error("Cannot open real-time event log");
        publisher_=create_publisher<interfaces::msg::BridgeState>("/bridge/state",rclcpp::QoS(1).reliable());
        joints_=create_publisher<sensor_msgs::msg::JointState>("/joint_states",rclcpp::SensorDataQoS());
        service_=create_service<interfaces::srv::Command>("/bridge/command",
            [this](const std::shared_ptr<interfaces::srv::Command::Request> request,
                   std::shared_ptr<interfaces::srv::Command::Response> response) { command(*request,*response); });
        catalog_=create_service<interfaces::srv::ControllerCatalog>("/bridge/controllers",
            [this](const std::shared_ptr<interfaces::srv::ControllerCatalog::Request>,
                   std::shared_ptr<interfaces::srv::ControllerCatalog::Response> response) { catalog(*response); });
        timer_=create_wall_timer(std::chrono::milliseconds(20),[this]{ drain(); publish(); });
        worker_=std::thread([this]{ run(); });
        RCLCPP_INFO(get_logger(),"%s bridge started disarmed",mock_?"Mock":"Physical SPI");
    }
    ~Bridge() override {
        quit_=true;
        if(worker_.joinable()) worker_.join();
        drain(); finish_recording();
    }
private:
    bool mock_;
    std::string device_,directory_;
    Control core_;
    std::unique_ptr<Profiles> profiles_;
    Queue<Command,128> commands_;
    Queue<Sample,4096> samples_;
    Queue<RealtimeEvent,256> events_;
    std::atomic<uint64_t> lost_events_{0},lost_periods_{0};
    std::ofstream diagnostics_;
    bool diagnostics_write_failed_=false;
    std::atomic<bool> quit_{false},stop_{false};
    std::thread worker_;
    Sample latest_{};
    uint64_t next_command_=0,record_id_=0,recorded_=0,last_recorded_sample_=0;
    bool stopping_record_=false;
    std::ofstream csv_;
    std::string recording_file_,recording_error_;
    rclcpp::Publisher<interfaces::msg::BridgeState>::SharedPtr publisher_;
    rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr joints_;
    rclcpp::Service<interfaces::srv::Command>::SharedPtr service_;
    rclcpp::Service<interfaces::srv::ControllerCatalog>::SharedPtr catalog_;
    rclcpp::TimerBase::SharedPtr timer_;

    static interfaces::msg::ControllerProfile profile_message(const ProfileId& id,const ControllerConfig& config) {
        interfaces::msg::ControllerProfile msg;const auto& type=controller_types[config.type];
        msg.id=id.data();msg.type=type.id;
        for(unsigned i=0;i<type.count;i++) {msg.parameter_names.push_back(type.parameters[i].name);msg.parameter_values.push_back(config.parameters[i]);}
        return msg;
    }
    void catalog(interfaces::srv::ControllerCatalog::Response& out) {
        for(const auto& type:controller_types) {
            interfaces::msg::ControllerType msg;msg.type=type.id;msg.label=type.label;
            for(unsigned i=0;i<type.count;i++) {
                const auto& p=type.parameters[i];msg.parameter_names.push_back(p.name);msg.parameter_units.push_back(p.unit);
                msg.defaults.push_back(p.initial);msg.minimum.push_back(p.minimum);msg.maximum.push_back(p.maximum);
            }
            out.types.push_back(msg);
        }
        try {
            for(const auto& id:profiles_->ids()) {
                try {const auto p=profiles_->load(id);out.profiles.push_back(profile_message(p.id,p.controller));}
                catch(const std::exception& e){out.errors.push_back(id+": "+e.what());}
            }
        } catch(const std::exception& e){out.errors.push_back(e.what());}
    }

    void command(const interfaces::srv::Command::Request& req,interfaces::srv::Command::Response& out) {
        Command c{}; c.axis=req.axis; c.value=req.value; c.enabled=req.enabled;
        if((c.axis>=2 && !(req.operation=="digital"&&c.axis<4))||!std::isfinite(c.value)) {out.message="Invalid axis or non-finite value";return;}
        const auto& op=req.operation;
        if(op=="target") c.op=Op::target;
        else if(op=="pwm") c.op=Op::pwm;
        else if(op=="mode") c.op=Op::mode;
        else if(op=="axis") c.op=Op::axis;
        else if(op=="system") c.op=Op::system;
        else if(op=="stop") {c.op=Op::stop;stop_=true;}
        else if(op=="zero") c.op=Op::zero;
        else if(op=="clear") c.op=Op::clear;
        else if(op=="kp") c.op=Op::kp;
        else if(op=="ki") c.op=Op::ki;
        else if(op=="kd") c.op=Op::kd;
        else if(op=="limit") c.op=Op::limit;
        else if(op=="digital") c.op=Op::digital;
        else if(op=="analog") c.op=Op::analog;
        else if(op=="controller_apply"||op=="profile_load"||op=="profile_save"||op=="profile_create") {
            drain();
            if(!latest_.connected){out.message="Control bridge is not connected";return;}
            try {
                if(op=="controller_apply") {
                    c.op=Op::controller;c.controller=Profiles::parameters(req.controller_type,req.parameter_names,req.parameter_values);
                } else if(op=="profile_load") {
                    // shortcut: startup assignment stays in bridge.yaml until project configuration editing is added.
                    const auto profile=profiles_->load(req.profile);
                    c.op=Op::controller;c.controller=profile.controller;c.profile=profile.id;
                } else {
                    if(latest_.command_id!=next_command_||req.controller_revision!=latest_.controller_revision[c.axis]) {
                        out.message="Controller changed or a command is pending; refresh before saving";return;
                    }
                    if(op=="profile_save"&&req.profile!=latest_.profiles[c.axis].data()) {
                        out.message="Save applies to the current profile; use Save as for a new profile";return;
                    }
                    profiles_->save(req.profile,latest_.controllers[c.axis],op=="profile_create");
                    c.op=Op::profile;c.profile=profile_id(req.profile);
                }
            } catch(const std::exception& e){out.message=e.what();return;}
        }
        else if(op=="record_start") {
            if(csv_.is_open()) {out.message="A recording is already active";return;}
            const auto id=static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
            recording_file_="session-"+std::to_string(id)+".csv";
            csv_.open(fs::path(directory_)/recording_file_);
            if(!csv_) {csv_.clear();out.message="Cannot create recording";return;}
            csv_<<"# project="<<DIAMOND_PROJECT_ID<<"; axis0_counts_per_mm="<<std::setprecision(15)<<latest_.scale[0]
                <<"; axis1_counts_per_mm="<<latest_.scale[1]<<"; mock="<<mock_<<"\n";
            csv_<<"timestamp_us,sample,axis0_target_mm,axis0_reference_mm,axis0_position_mm,axis0_raw_counts,axis0_zero_counts,axis0_requested_duty,axis0_output_duty,axis0_enabled,axis0_kp,axis0_ki,axis0_kd,axis0_duty_limit,axis0_mode,axis0_pwm_target,axis1_target_mm,axis1_reference_mm,axis1_position_mm,axis1_raw_counts,axis1_zero_counts,axis1_requested_duty,axis1_output_duty,axis1_enabled,axis1_kp,axis1_ki,axis1_kd,axis1_duty_limit,axis1_mode,axis1_pwm_target,dsp_sequence,dsp_command_sequence,dsp_arm_mask,dsp_fault,digital_in,digital_out,analog_valid,ai0_raw,ai1_raw,ai2_raw,ai3_raw,ai4_raw,ai5_raw,ao0_raw,ao1_raw,encoder_valid,encoder0_counts,encoder1_counts,encoder2_counts,encoder3_counts,encoder4_counts,encoder5_counts,axis0_controller_type,axis0_controller_profile,axis0_controller_parameters,axis1_controller_type,axis1_controller_profile,axis1_controller_parameters\n";
            recorded_=0;last_recorded_sample_=0; recording_error_.clear(); stopping_record_=false;
            record_id_++;
            c.op=Op::record;c.value=static_cast<double>(record_id_);
        } else if(op=="record_stop") {
            if(!csv_.is_open()) {out.message="No recording active";return;}
            c.op=Op::record;c.value=0;stopping_record_=true;
        } else {out.message="Unknown operation";return;}
        c.id=++next_command_;
        if(!commands_.push(c)) {
            if(op=="record_start") finish_recording();
            if(op=="record_stop") stopping_record_=false;
            out.message="Control command queue is full";return;
        }
        out.accepted=true;out.command_id=c.id;out.message="Queued";
    }

    void finish_recording() {
        if(!csv_.is_open()) return;
        csv_.flush();
        if(!csv_) recording_error_="Recording write failed (check disk space)";
        csv_.close();csv_.clear();stopping_record_=false;
    }
    void drain() {
        RealtimeEvent event;
        while(events_.pop(event)) {
            write_event(diagnostics_,event);diagnostics_.flush();
            const auto& t=event.history[event.count-1];
            RCLCPP_ERROR(get_logger(),"RT event cycle=%lu missed_periods=%lu work=%.3fus SPI=%.3fus late=%.3fus: %s",
                t.number,event.missed_periods,t.work_us,t.spi_us,t.late_us,event.error.data());
        }
        const auto lost=lost_events_.exchange(0),periods=lost_periods_.exchange(0);
        if(lost||periods) {
            diagnostics_<<"{\"error\":\"diagnostic_queue_overflow\",\"events_without_context\":"<<lost
                        <<",\"missed_periods_without_context\":"<<periods<<"}\n";
            diagnostics_.flush();
            RCLCPP_ERROR(get_logger(),"Diagnostic queue overflow: %lu events, %lu missed periods; raw context unavailable",lost,periods);
        }
        if(!diagnostics_&&!diagnostics_write_failed_) {
            diagnostics_write_failed_=true;
            RCLCPP_ERROR(get_logger(),"Real-time event file write failed; event summaries continue in this journal");
        }
        Sample s;
        while(samples_.pop(s)) {
            latest_=s;
            if(csv_.is_open() && s.record_id==record_id_ && s.connected && s.number!=last_recorded_sample_) {
                last_recorded_sample_=s.number;
                csv_<<s.timestamp<<','<<s.number;
                for(unsigned a=0;a<2;a++) {
                    csv_<<','<<s.target[a]<<','<<s.reference[a]<<','<<s.position[a]<<','<<s.telemetry.position[a]
                        <<','<<s.zero[a]<<','<<s.telemetry.requested_duty[a]<<','<<s.telemetry.duty[a]
                        <<','<<(s.system&&s.selected[a]&&s.fault==Error::none)
                        <<','<<s.tuning[a].kp<<','<<s.tuning[a].ki<<','<<s.tuning[a].kd<<','<<s.tuning[a].limit
                        <<','<<static_cast<unsigned>(s.mode[a])<<','<<s.pwm[a];
                }
                csv_<<','<<s.telemetry.sequence<<','<<s.telemetry.command_sequence<<','<<s.telemetry.arm_mask
                    <<','<<s.telemetry.fault<<','<<s.telemetry.digital_in<<','<<s.telemetry.digital_out<<','<<s.telemetry.analog_valid;
                for(auto value:s.telemetry.analog_in)csv_<<','<<value;
                for(auto value:s.telemetry.analog_out)csv_<<','<<value;
                csv_<<','<<s.telemetry.encoder_valid;
                for(auto value:s.telemetry.position)csv_<<','<<value;
                for(unsigned a=0;a<2;a++) {
                    const auto& config=s.controllers[a];const auto& type=controller_types[config.type];
                    csv_<<','<<type.id<<','<<s.profiles[a].data()<<',';
                    csv_<<std::setprecision(17);
                    for(unsigned i=0;i<type.count;i++)csv_<<(i?";":"")<<type.parameters[i].name<<'='<<config.parameters[i];
                    csv_<<std::setprecision(15);
                }
                csv_<<'\n';recorded_++;
                if(!csv_) finish_recording();
            } else if(csv_.is_open() && stopping_record_ && s.record_id==0) finish_recording();
        }
    }
    void publish() {
        const auto& s=latest_;
        interfaces::msg::BridgeState msg;
        msg.project_id=DIAMOND_PROJECT_ID;msg.position_controller="PID";
        msg.reference_speed_mm_s=core_.speed_mm_s;
        msg.controller_revision=s.controller_revision;
        msg.control_mode=s.mode;msg.pwm_target=s.pwm;msg.counts_per_mm=s.scale;
        msg.minimum_mm=s.minimum;msg.maximum_mm=s.maximum;
        msg.sample_count=s.number;msg.timestamp_us=s.timestamp;msg.connected=s.connected;msg.mock=mock_;
        msg.system_enabled=s.system;msg.position_mm=s.position;msg.target_mm=s.target;msg.reference_mm=s.reference;
        msg.zero_counts=s.zero;
        for(unsigned a=0;a<2;a++) {
            msg.controllers[a]=profile_message(s.profiles[a],s.controllers[a]);
            msg.raw_counts[a]=s.telemetry.position[a];msg.requested_duty[a]=s.telemetry.requested_duty[a];
            msg.output_duty[a]=s.telemetry.duty[a];msg.axis_enabled[a]=s.selected[a];
            msg.kp[a]=s.tuning[a].kp;msg.ki[a]=s.tuning[a].ki;msg.kd[a]=s.tuning[a].kd;msg.duty_limit[a]=s.tuning[a].limit;
        }
        msg.fault=error_text(s.fault);msg.command_error=error_text(s.command_error);msg.last_command_id=s.command_id;
        msg.deadline_misses=s.deadlines;msg.telemetry_errors=s.errors;msg.dropped_samples=s.dropped;
        msg.max_cycle_us=s.max_cycle_us;msg.max_lateness_us=s.max_lateness_us;
        msg.protocol_version=PCD_VERSION;msg.control_rate_hz=kRateHz;
        msg.dsp_state=s.telemetry.state;msg.dsp_arm_mask=s.telemetry.arm_mask;msg.dsp_fault=s.telemetry.fault;
        msg.dsp_sequence=s.telemetry.sequence;msg.dsp_command_sequence=s.telemetry.command_sequence;
        std::copy_n(s.telemetry.position,6,msg.encoder_counts.begin());
        msg.encoder_valid=s.telemetry.encoder_valid;msg.digital_in=s.telemetry.digital_in;msg.digital_out=s.telemetry.digital_out;
        msg.analog_valid=s.telemetry.analog_valid;msg.analog_out_valid=s.telemetry.analog_out_valid;
        std::copy_n(s.telemetry.analog_in,6,msg.analog_in.begin());std::copy_n(s.telemetry.analog_out,2,msg.analog_out.begin());
        msg.dsp_invalid_frames=s.telemetry.invalid_frames;msg.dsp_rejected_commands=s.telemetry.rejected_commands;
        msg.dsp_watchdogs=s.telemetry.watchdogs;msg.encoder_errors=s.telemetry.encoder_errors;
        msg.cpu2_max_poll_us=s.telemetry.cpu2_max_poll_us;
        msg.recording=csv_.is_open();msg.recording_file=recording_file_;msg.recorded_samples=recorded_;
        msg.recording_error=recording_error_;
        publisher_->publish(msg);
        sensor_msgs::msg::JointState j;
        j.header.stamp=now();j.name={"axis0","axis1"};j.position={s.position[0]/1000,s.position[1]/1000};
        joints_->publish(j);
    }

    void run() {
        std::unique_ptr<SpiLink> link;
        bool realtime_ready=mock_;
        try {if(!mock_) link=std::make_unique<SpiLink>(device_.c_str());}
        catch(const std::exception& e) {RCLCPP_ERROR(get_logger(),"SPI setup: %s",e.what());core_.fail(Error::telemetry);core_.state.errors++;}
        if(!mock_ && link) {
            cpu_set_t cpu;CPU_ZERO(&cpu);CPU_SET(3,&cpu);
            sched_param priority{};priority.sched_priority=80;
            if(pthread_setaffinity_np(pthread_self(),sizeof(cpu),&cpu)!=0 ||
               mlockall(MCL_CURRENT)!=0 || pthread_setschedparam(pthread_self(),SCHED_FIFO,&priority)!=0) {
                core_.fail(Error::realtime);
            } else realtime_ready=true;
        }
        Telemetry simulated{};
        std::array<double,2> mock_position{};
        uint16_t sent_mask=0;bool sent_active=false;
        auto scheduled=Clock::now();
        CycleHistory history;
        uint64_t iteration=0;
        while(!quit_) {
            const auto begin=Clock::now();
            const auto deadline=scheduled+control_period;
            const auto late=std::chrono::duration<double,std::micro>(begin-scheduled).count();
            core_.state.max_lateness_us=std::max(core_.state.max_lateness_us,late);
            Command c;
            uint16_t commands=0;
            for(;commands<128 && commands_.pop(c);++commands) core_.apply(c);
            // Stop wins over queued enables, even when the command queue was full.
            if(stop_.exchange(false)) core_.apply({Op::stop,0,0,false,core_.state.command_id});
            if(link && !realtime_ready) core_.fail(Error::realtime);
            const auto values=core_.duty();
            const auto mask=core_.mask();
            uint16_t operation=0;
            std::array<char,160> error{};
            try {
                if(mock_) {
                    core_.dsp_clear_requested=false;
                    simulated.timestamp_us+=1'000'000/kRateHz;simulated.sequence++;
                    simulated.encoder_valid=63;simulated.arm_mask=mask;simulated.state=core_.state.system?1:0;
                    simulated.analog_valid=63;simulated.analog_out_valid=3;
                    simulated.digital_in=simulated.digital_out=core_.state.digital_out;
                    std::copy(core_.state.analog_out.begin(),core_.state.analog_out.end(),simulated.analog_out);
                    std::copy(core_.state.analog_out.begin(),core_.state.analog_out.end(),simulated.analog_in);
                    for(unsigned a=0;a<2;a++) {
                        // shortcut: deterministic first-order plant, use hardware for controller validation.
                        if(mask&(1U<<a)) {
                            if(core_.state.mode[a]==1) mock_position[a]+=values[a]*core_.pid[a].sign*50*dt*core_.state.scale[a];
                            else mock_position[a]+=(core_.state.zero[a]+core_.state.reference[a]*core_.state.scale[a]-mock_position[a])*0.08;
                        }
                        simulated.position[a]=static_cast<int32_t>(std::llround(mock_position[a]));
                        simulated.requested_duty[a]=simulated.duty[a]=values[a];
                    }
                    core_.receive(simulated);
                } else if(link) {
                    const auto op=core_.dsp_clear_requested?PCD_CLEAR:!core_.state.system?PCD_DISARM:
                        (!sent_active||mask!=sent_mask)?PCD_ARM:PCD_CYCLE;
                    operation=op;
                    std::array<int32_t,6> references{};
                    for(unsigned a=0;a<2;a++) references[a]=static_cast<int32_t>(static_cast<uint32_t>(
                        std::llround(core_.state.zero[a]+core_.state.reference[a]*core_.state.scale[a])));
                    const auto telemetry=link->exchange(op,op==PCD_CYCLE?values:Values{},mask,references,
                        core_.state.digital_out,core_.state.analog_out);
                    for(unsigned a=0;a<2;a++) if(!std::isfinite(telemetry.duty[a])||!std::isfinite(telemetry.requested_duty[a]))
                        throw std::runtime_error("Invalid numeric telemetry");
                    if(core_.dsp_clear_requested && !telemetry.fault){
                        core_.dsp_clear_requested=false;
                        if(core_.state.fault==Error::dsp)core_.state.fault=Error::none;
                    }
                    core_.receive(telemetry);sent_mask=mask;sent_active=core_.state.system;

                }
            } catch(const std::exception& e) {
                std::strncpy(error.data(),e.what(),error.size()-1);
                core_.state.errors++;
                // Initial SPI pipeline fill is expected while all outputs are disabled.
                if(core_.initialized || core_.state.errors>10) core_.fail(Error::telemetry);
                core_.state.connected=false;sent_mask=0;sent_active=false;
            }
            const auto end=Clock::now();
            const auto work_us=std::chrono::duration<double,std::micro>(end-begin).count();
            const auto missed=missed_periods(scheduled,end);
            core_.state.max_cycle_us=std::max(core_.state.max_cycle_us,work_us);
            history.push({++iteration,static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(begin.time_since_epoch()).count()),
                work_us,late,link?link->diagnostics().duration_ns/1000.0:0,core_.state.telemetry.sequence,
                core_.state.telemetry.timestamp_us,core_.state.telemetry.command_sequence,operation,mask,commands});
            if(!mock_ && (error[0] || end>deadline)) {
                RealtimeEvent event;history.copy(event);event.deadline=missed!=0;event.missed_periods=missed;event.error=error;
                if(link)event.spi=link->diagnostics();
                if(!events_.push(event)){++lost_events_;lost_periods_+=missed;}
            }
            if(end>deadline) {
                core_.state.deadlines+=missed;
                // Mock uses ordinary scheduling; only hardware enforces the RT deadline.
                if(mask!=0 && !mock_) core_.fail(Error::deadline);
                // Do not issue back-to-back SPI frames after an overrun: allow DSP rearm.
                scheduled=end+control_period;
            } else scheduled=deadline;
            if(!samples_.push(core_.state)) core_.state.dropped++;
            std::this_thread::sleep_until(scheduled);
        }
        // SpiLink sends DISARM on destruction, including process shutdown.
    }
};
int main(int argc,char** argv) {
    rclcpp::init(argc,argv);
    try {rclcpp::spin(std::make_shared<Bridge>());}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';rclcpp::shutdown();return 1;}
    rclcpp::shutdown();return 0;
}

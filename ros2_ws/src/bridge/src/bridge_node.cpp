#include "bridge/control.hpp"
#include "bridge/profiles.hpp"
#include "bridge/diagnostics.hpp"
#include "bridge/inputs.hpp"
#include "bridge/runner.hpp"
#include "bridge/recorder.hpp"
#include <interfaces/msg/data_preview.hpp>
#include <interfaces/srv/signal_catalog.hpp>
#include <interfaces/srv/logger_command.hpp>
#include <interfaces/srv/load_program.hpp>
#include <interfaces/srv/runner_command.hpp>
#include <rclcpp/rclcpp.hpp>
#include <interfaces/msg/bridge_state.hpp>
#include <interfaces/msg/command_result.hpp>
#include <interfaces/msg/position_command.hpp>
#include <interfaces/msg/duty_command.hpp>
#include <interfaces/msg/digital_command.hpp>
#include <interfaces/msg/analog_command.hpp>
#include <interfaces/srv/dsp_command.hpp>
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
        rcl_interfaces::msg::ParameterDescriptor deadline_policy;
        deadline_policy.read_only=true;
        deadline_policy.description="Project startup policy: disarm on a missed control deadline; timing violations are always logged";
        disarm_on_deadline_miss_=declare_parameter("disarm_on_deadline_miss",true,deadline_policy);
        device_=declare_parameter("spi_device",std::string("/dev/spidev0.0"));
        directory_=declare_parameter("recording_directory",std::string("recordings"));
        profiles_=std::make_unique<Profiles>(declare_parameter("controller_directory",std::string("projects/")+DIAMOND_PROJECT_ID+"/controllers"));
        for(unsigned a=0;a<kAxes;a++) {
            const auto key="axis"+std::to_string(a)+".";
            auto& controller=core_.controllers[a];
            const bool position=declare_parameter(key+"position_control",a<2);
            core_.state.position_configured[a]=position;
            core_.state.mode[a]=position?0:1;
            core_.state.scale[a]=declare_parameter(key+"counts_per_mm",counts_per_mm);
            core_.state.minimum[a]=declare_parameter(key+"minimum_mm",-50.0);
            core_.state.maximum[a]=declare_parameter(key+"maximum_mm",50.0);
            if(position && (!std::isfinite(core_.state.scale[a]) || core_.state.scale[a]<=0 ||
               !std::isfinite(core_.state.minimum[a]) || !std::isfinite(core_.state.maximum[a]) ||
               core_.state.minimum[a]>=core_.state.maximum[a] || core_.state.minimum[a]>0 || core_.state.maximum[a]<0))
                throw std::runtime_error("Invalid axis calibration or slider range");
            if(!position){core_.state.scale[a]=0;core_.state.minimum[a]=core_.state.maximum[a]=0;}
            controller.sign=declare_parameter(key+"motor_sign",-1.0);
            controller.limit=declare_parameter(key+"duty_limit",1.0);
            if((position&&controller.sign!=1&&controller.sign!=-1)||!(controller.limit>0&&controller.limit<=1))
                throw std::runtime_error("Invalid axis tuning");
            const auto profile_name=declare_parameter(key+"controller_profile",position?std::string("bench_pid"):std::string{});
            if(position) {
                const auto profile=profiles_->load(profile_name);
                controller.configure(profile.controller,false);
                core_.state.profiles[a]=profile.id;
            }
        }
        fs::create_directories(directory_);
        recorder_=std::make_unique<Recorder>(directory_);
        diagnostics_.open(fs::path(directory_)/"realtime-events.jsonl",std::ios::app);
        if(!diagnostics_)throw std::runtime_error("Cannot open real-time event log");
        publisher_=create_publisher<interfaces::msg::BridgeState>("/bridge/state",rclcpp::QoS(1).reliable());
        dsp_publisher_=create_publisher<interfaces::msg::DspState>("/dsp/state",rclcpp::QoS(1).reliable());
        // Keep DDS callbacks outside the RT thread. Values are volatile, bounded depth 16,
        // then coalesced per channel in an allocation-free mailbox.
        const auto input_qos=rclcpp::QoS(16).reliable().durability_volatile();
        target_sub_=create_subscription<interfaces::msg::PositionCommand>("/bridge/target",input_qos,
            [this](const interfaces::msg::PositionCommand& m){input(Op::target,m.axis,m.position_mm);});
        duty_sub_=create_subscription<interfaces::msg::DutyCommand>("/dsp/duty",input_qos,
            [this](const interfaces::msg::DutyCommand& m){input(Op::pwm,m.axis,m.duty);});
        digital_sub_=create_subscription<interfaces::msg::DigitalCommand>("/dsp/digital_out",input_qos,
            [this](const interfaces::msg::DigitalCommand& m){input(Op::digital,m.channel,m.value?1:0);});
        analog_sub_=create_subscription<interfaces::msg::AnalogCommand>("/dsp/analog_out",input_qos,
            [this](const interfaces::msg::AnalogCommand& m){input(Op::analog,m.channel,m.value);});
        dsp_service_=create_service<interfaces::srv::DspCommand>("/dsp/command",
            [this](const std::shared_ptr<interfaces::srv::DspCommand::Request> request,
                   std::shared_ptr<interfaces::srv::DspCommand::Response> response) {
                Command c{};
                if(request->operation==interfaces::srv::DspCommand::Request::DISARM){c.op=Op::stop;stop_=true;}
                else if(request->operation==interfaces::srv::DspCommand::Request::CLEAR_FAULT)c.op=Op::clear_dsp;
                else {response->message="Unsupported DSP operation (v5 has no reboot command)";return;}
                enqueue(c,*response);
            });
        preview_publisher_=create_publisher<interfaces::msg::DataPreview>("/data/preview",rclcpp::QoS(1).best_effort());
        signals_service_=create_service<interfaces::srv::SignalCatalog>("/data/signals",
            [this](const interfaces::srv::SignalCatalog::Request::SharedPtr,interfaces::srv::SignalCatalog::Response::SharedPtr out){
                for(const auto& field:recorder_->catalog){interfaces::msg::Signal msg;msg.name=field.name;msg.unit=field.unit;msg.group=field.group;msg.numeric=bool(field.number);out->signals.push_back(msg);}
            });
        logger_service_=create_service<interfaces::srv::LoggerCommand>("/data/logger",
            [this](const interfaces::srv::LoggerCommand::Request::SharedPtr req,interfaces::srv::LoggerCommand::Response::SharedPtr out){logger_command(*req,*out);});
        program_service_=create_service<interfaces::srv::LoadProgram>("/data/program",
            [this](const interfaces::srv::LoadProgram::Request::SharedPtr req,interfaces::srv::LoadProgram::Response::SharedPtr out){load_program(*req,*out);});
        runner_service_=create_service<interfaces::srv::RunnerCommand>("/data/runner",
            [this](const interfaces::srv::RunnerCommand::Request::SharedPtr req,interfaces::srv::RunnerCommand::Response::SharedPtr out){runner_command(*req,*out);});
        joints_=create_publisher<sensor_msgs::msg::JointState>("/joint_states",rclcpp::SensorDataQoS());
        service_=create_service<interfaces::srv::Command>("/bridge/command",
            [this](const std::shared_ptr<interfaces::srv::Command::Request> request,
                   std::shared_ptr<interfaces::srv::Command::Response> response) { command(*request,*response); });
        catalog_=create_service<interfaces::srv::ControllerCatalog>("/bridge/controllers",
            [this](const std::shared_ptr<interfaces::srv::ControllerCatalog::Request>,
                   std::shared_ptr<interfaces::srv::ControllerCatalog::Response> response) { catalog(*response); });
        result_publisher_=create_publisher<interfaces::msg::CommandResult>("/bridge/command_result",rclcpp::QoS(256).reliable());
        next_command_=static_cast<uint64_t>(Clock::now().time_since_epoch().count());
        core_.state.command_id=next_command_;
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
    bool mock_,disarm_on_deadline_miss_;
    std::string device_,directory_;
    Control core_;
    Runner runner_;
    std::unique_ptr<Recorder> recorder_;
    std::unique_ptr<Program> program_,pending_program_;
    uint64_t program_command_id_=0,record_command_id_=0,preview_sequence_=0;
    rclcpp::Publisher<interfaces::msg::DataPreview>::SharedPtr preview_publisher_;
    rclcpp::Service<interfaces::srv::SignalCatalog>::SharedPtr signals_service_;
    rclcpp::Service<interfaces::srv::LoggerCommand>::SharedPtr logger_service_;
    rclcpp::Service<interfaces::srv::LoadProgram>::SharedPtr program_service_;
    rclcpp::Service<interfaces::srv::RunnerCommand>::SharedPtr runner_service_;
    std::unique_ptr<Profiles> profiles_;
    Queue<Command,128> commands_;
    Inputs inputs_;
    struct Result {
        uint64_t id;Error error;
        bool has_controller=false;unsigned axis=0;uint64_t revision=0;
        ProfileId profile{};ControllerConfig controller{};double limit=0;
    };
    Queue<Result,256> results_;
    std::atomic<uint64_t> lost_results_{0};
    Queue<Sample,4096> samples_;
    Queue<RealtimeEvent,256> events_;
    std::atomic<uint64_t> lost_events_{0},lost_periods_{0};
    std::ofstream diagnostics_;
    bool diagnostics_write_failed_=false;
    std::atomic<bool> quit_{false},stop_{false};
    std::thread worker_;
    Sample latest_{};
    uint64_t next_command_=0,next_order_=0,last_dsp_sample_=0,record_id_=0;
    rclcpp::Publisher<interfaces::msg::BridgeState>::SharedPtr publisher_;
    rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr joints_;
    rclcpp::Publisher<interfaces::msg::DspState>::SharedPtr dsp_publisher_;
    rclcpp::Publisher<interfaces::msg::CommandResult>::SharedPtr result_publisher_;
    rclcpp::Subscription<interfaces::msg::PositionCommand>::SharedPtr target_sub_;
    rclcpp::Subscription<interfaces::msg::DutyCommand>::SharedPtr duty_sub_;
    rclcpp::Subscription<interfaces::msg::DigitalCommand>::SharedPtr digital_sub_;
    rclcpp::Subscription<interfaces::msg::AnalogCommand>::SharedPtr analog_sub_;
    rclcpp::Service<interfaces::srv::DspCommand>::SharedPtr dsp_service_;
    rclcpp::Service<interfaces::srv::Command>::SharedPtr service_;
    rclcpp::Service<interfaces::srv::ControllerCatalog>::SharedPtr catalog_;
    rclcpp::TimerBase::SharedPtr timer_;

    static interfaces::msg::ControllerProfile profile_message(const ProfileId& id,const ControllerConfig& config) {
        interfaces::msg::ControllerProfile msg;const auto& type=controller_types[config.type];
        msg.id=id.data();msg.type=type.id;msg.slew_enabled=config.slew_enabled;msg.slew_rate_mm_s=config.slew_rate_mm_s;
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

    void input(Op op,unsigned channel,double value) {
        Command c{op,channel,value};c.order=++next_order_;inputs_.publish(c);
    }
    template<class Response> void enqueue(Command c,Response& out) {
        c.id=next_command_+1;c.order=++next_order_;
        if(!commands_.push(c)) {out.message="Control command queue is full";return;}
        next_command_=c.id;out.accepted=true;out.command_id=c.id;out.message="Queued; observe bridge state for application";
    }
    void command(const interfaces::srv::Command::Request& req,interfaces::srv::Command::Response& out) {
        Command c{}; c.axis=req.axis; c.value=req.value; c.enabled=req.enabled;
        if(c.axis>=kAxes||!std::isfinite(c.value)) {out.message="Invalid axis or non-finite value";return;}
        const auto& op=req.operation;
        if(op=="mode") c.op=Op::mode;
        else if(op=="axis") c.op=Op::axis;
        else if(op=="system") c.op=Op::system;
        else if(op=="stop") {c.op=Op::stop;stop_=true;}
        else if(op=="zero") c.op=Op::zero;
        else if(op=="clear") c.op=Op::clear;
        else if(op=="kp") c.op=Op::kp;
        else if(op=="ki") c.op=Op::ki;
        else if(op=="kd") c.op=Op::kd;
        else if(op=="limit") c.op=Op::limit;
        else if(op=="controller_apply"||op=="profile_load"||op=="profile_save"||op=="profile_create") {
            drain();
            if(!latest_.connected){out.message="Control bridge is not connected";return;}
            try {
                if(op=="controller_apply") {
                    c.op=Op::controller;c.controller=Profiles::parameters(req.controller_type,req.parameter_names,req.parameter_values);
                    c.controller.slew_enabled=req.set_reference?req.slew_enabled:latest_.controllers[c.axis].slew_enabled;
                    c.controller.slew_rate_mm_s=req.set_reference?req.slew_rate_mm_s:latest_.controllers[c.axis].slew_rate_mm_s;
                    if(!valid_controller(c.controller))throw std::runtime_error("Invalid reference processing parameters");
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
        else if(op=="record_start"||op=="record_stop") {
            interfaces::srv::LoggerCommand::Request r;r.operation=op=="record_start"?"start":"stop";
            logger_command(r,out);return;
        } else {out.message="Unknown operation";return;}
        enqueue(c,out);
    }
    void finish_recording(){recorder_->finish();}
    template<class Response> void logger_command(const interfaces::srv::LoggerCommand::Request& req,Response& out) {
        drain();
        Command c{};c.op=Op::record;
        if(req.operation=="start") {
            if(!latest_.connected){out.message="DSP is not connected";return;}
            try{recorder_->start(++record_id_,req.signals,latest_,DIAMOND_PROJECT_ID);}
            catch(const std::exception& e){out.message=e.what();return;}
            c.value=static_cast<double>(record_id_);
        }else if(req.operation!="stop"){out.message="Unknown logger operation";return;}
        enqueue(c,out);
        if(req.operation=="start") {
            if(!out.accepted)finish_recording();else record_command_id_=out.command_id;
        }
    }
    void load_program(const interfaces::srv::LoadProgram::Request& req,interfaces::srv::LoadProgram::Response& out) {
        drain();
        if(pending_program_||runner_.owns(latest_)||latest_.command_id!=next_command_){out.message="Stop the runner and wait for pending commands before loading";return;}
        auto candidate=std::make_unique<Program>();candidate->id=next_command_+1;
        candidate->name=req.name;candidate->channels=req.channels;candidate->values=req.values;
        try{validate_program(*candidate,latest_);}catch(const std::exception& e){out.message=e.what();return;}
        Command c{};c.op=Op::runner_load;c.program=candidate.get();
        enqueue(c,out);
        if(out.accepted){program_command_id_=out.command_id;pending_program_=std::move(candidate);}
    }
    void runner_command(const interfaces::srv::RunnerCommand::Request& req,interfaces::srv::RunnerCommand::Response& out) {
        drain();Command c{};
        if(req.operation=="stop"){c.op=Op::runner_stop;enqueue(c,out);return;}
        if(req.operation!="start"){out.message="Unknown runner operation";return;}
        if(pending_program_||!program_||runner_.owns(latest_)||latest_.command_id!=next_command_){out.message="Load a program, stop any active run, and wait for pending commands";return;}
        if(req.completion!="hold"&&req.completion!="stop"&&req.completion!="loop"){out.message="Completion must be hold, stop or loop";return;}
        if(!latest_.connected||latest_.fault!=Error::none){out.message="Connect and clear the host fault before starting";return;}
        c.check_stop=true;c.stop_count=req.check_stop_count?req.expected_stop_count:latest_.stop_count;
        c.op=Op::runner_start;c.value=req.completion=="hold"?0:req.completion=="stop"?1:2;c.enabled=req.auto_enable;
        if(req.record) {
            try{recorder_->start(++record_id_,req.signals,latest_,DIAMOND_PROJECT_ID);c.record=record_id_;}
            catch(const std::exception& e){out.message=e.what();return;}
        }
        enqueue(c,out);
        if(req.record){if(!out.accepted)finish_recording();else record_command_id_=out.command_id;}
    }

    void drain() {
        Result result;
        while(results_.pop(result)) {
            interfaces::msg::CommandResult msg;msg.command_id=result.id;
            msg.applied=result.error==Error::none;msg.message=error_text(result.error);
            msg.has_controller=result.has_controller;
            if(result.has_controller) {
                msg.axis=result.axis;msg.controller_revision=result.revision;
                msg.controller=profile_message(result.profile,result.controller);msg.duty_limit=result.limit;
            }
            result_publisher_->publish(msg);
            if(result.id==record_command_id_&&result.error!=Error::none)finish_recording();
            if(result.id==program_command_id_&&result.error!=Error::none)pending_program_.reset();
        }
        if(const auto lost=lost_results_.exchange(0))RCLCPP_ERROR(get_logger(),"Command result queue overflow: %lu results lost",lost);
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
        interfaces::msg::DataPreview preview;
        const auto count=recorder_->catalog.size();
        preview.latest.resize(count);preview.minimum.assign(count,INFINITY);preview.maximum.assign(count,-INFINITY);
        bool have_sample=false;
        while(samples_.pop(s)) {
            latest_=s;recorder_->write(s);
            if(!s.connected)continue;
            have_sample=true;preview.timestamp_us=s.timestamp;
            for(size_t i=0;i<count;i++) {
                const auto& field=recorder_->catalog[i];const auto v=field.number?field.number(s):0;
                preview.latest[i]=v;preview.minimum[i]=std::min(preview.minimum[i],v);preview.maximum[i]=std::max(preview.maximum[i],v);
            }
        }
        if(pending_program_&&latest_.program_id==pending_program_->id)program_=std::move(pending_program_);
        if(have_sample){preview.sequence=++preview_sequence_;preview_publisher_->publish(preview);}
    }
    void publish() {
        const auto& s=latest_;
        interfaces::msg::BridgeState msg;
        msg.project_id=DIAMOND_PROJECT_ID;

        msg.controller_revision=s.controller_revision;
        msg.control_mode=s.mode;msg.pwm_target=s.pwm;msg.counts_per_mm=s.scale;
        msg.minimum_mm=s.minimum;msg.maximum_mm=s.maximum;
        msg.sample_count=s.number;msg.timestamp_us=s.timestamp;msg.connected=s.connected;msg.mock=mock_;
        msg.system_enabled=s.system;msg.position_mm=s.position;msg.target_mm=s.target;msg.reference_mm=s.reference;
        msg.zero_counts=s.zero;
        for(unsigned a=0;a<kAxes;a++) {
            msg.controllers[a]=profile_message(s.profiles[a],s.controllers[a]);
            msg.axis_enabled[a]=s.selected[a];msg.duty_limit[a]=s.tuning[a].limit;
        }
        msg.fault=error_text(s.fault);msg.command_error=error_text(s.command_error);msg.last_command_id=s.command_id;
        msg.deadline_misses=s.deadlines;msg.telemetry_errors=s.errors;msg.dropped_samples=s.dropped;
        msg.max_cycle_us=s.max_cycle_us;msg.max_lateness_us=s.max_lateness_us;
        msg.protocol_version=PCD_VERSION;msg.control_rate_hz=kRateHz;
        msg.dsp_restarts=s.dsp_restarts;
        msg.position_configured=s.position_configured;
        msg.feedback_age_ms=s.feedback_received_ns ?
            (std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()-s.feedback_received_ns)/1e6 : -1;
        msg.input_rejections=s.input_rejections;msg.input_error=error_text(s.input_error);msg.input_channel=s.input_channel;
        msg.input_topic=s.input_operation==Op::target?"/bridge/target":s.input_operation==Op::pwm?"/dsp/duty":
            s.input_operation==Op::digital?"/dsp/digital_out":"/dsp/analog_out";
        const auto& t=s.telemetry;auto& d=msg.dsp;
        d.timestamp_us=t.timestamp_us;d.sequence=t.sequence;d.session=t.session;d.command_sequence=t.command_sequence;
        d.state=t.state;d.arm_mask=t.arm_mask;d.fault=t.fault;d.encoder_valid=t.encoder_valid;
        d.digital_in=t.digital_in;d.digital_out=t.digital_out;d.analog_valid=t.analog_valid;d.analog_out_valid=t.analog_out_valid;
        std::copy_n(t.analog_in,6,d.analog_in.begin());std::copy_n(t.analog_out,2,d.analog_out.begin());
        std::copy_n(t.position,6,d.position.begin());std::copy_n(t.reference,6,d.reference.begin());
        std::copy_n(t.requested_duty,6,d.requested_duty.begin());std::copy_n(t.duty,6,d.duty.begin());
        d.invalid_frames=t.invalid_frames;d.rejected_commands=t.rejected_commands;d.command_timeout_count=t.watchdogs;
        d.encoder_errors=t.encoder_errors;d.cpu2_max_poll_us=t.cpu2_max_poll_us;
        if(s.telemetry_count!=last_dsp_sample_) {dsp_publisher_->publish(d);last_dsp_sample_=s.telemetry_count;}
        msg.recording=recorder_->active();msg.recording_file=recorder_->filename;msg.recorded_samples=recorder_->samples;
        msg.recording_error=recorder_->error;msg.recorded_signals=recorder_->selected;
        msg.stop_count=s.stop_count;msg.program_id=s.program_id;msg.program_rows=s.runner_rows;
        const auto* p=pending_program_&&pending_program_->id==s.program_id?pending_program_.get():program_.get();
        if(p&&p->id==s.program_id)msg.program_name=p->name;
        msg.runner_state=s.runner_state;msg.runner_row=s.runner_row;msg.runner_cycles=s.runner_cycles;
        msg.runner_completion=s.runner_completion==0?"hold":s.runner_completion==1?"stop":"loop";
        msg.runner_error=error_text(s.runner_error);msg.runner_axis_mask=s.runner_axes;msg.runner_do_mask=s.runner_digital;msg.runner_ao_mask=s.runner_analog;
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
        if(!mock_) {
            cpu_set_t cpu;CPU_ZERO(&cpu);CPU_SET(3,&cpu);
            sched_param priority{};priority.sched_priority=80;
            if(pthread_setaffinity_np(pthread_self(),sizeof(cpu),&cpu)!=0 ||
               mlockall(MCL_CURRENT)!=0 || pthread_setschedparam(pthread_self(),SCHED_FIFO,&priority)!=0) {
                core_.fail(Error::realtime);
            } else realtime_ready=true;
        }
        Telemetry simulated{};
        std::array<double,6> mock_position{};
        uint16_t sent_mask=0;bool sent_active=false;
        auto scheduled=Clock::now(),next_open=scheduled+std::chrono::seconds(1),last_error_log=Clock::time_point{};
        std::array<char,160> previous_error{};
        uint64_t repeated_errors=0;
        CycleHistory history;
        uint64_t iteration=0;
        while(!quit_) {
            const auto begin=Clock::now();
            const auto deadline=scheduled+control_period;
            const auto late=std::chrono::duration<double,std::micro>(begin-scheduled).count();
            core_.state.max_lateness_us=std::max(core_.state.max_lateness_us,late);
            std::array<Command,128+Inputs::count> pending;
            uint16_t commands=0;
            for(;commands<128 && commands_.pop(pending[commands]);++commands){}
            commands+=inputs_.take(pending.data()+commands);
            std::sort(pending.begin(),pending.begin()+commands,[](const Command& a,const Command& b){return a.order<b.order;});
            for(unsigned i=0;i<commands;i++) {
                const auto& command=pending[i];
                if(command.op==Op::runner_load||command.op==Op::runner_start||command.op==Op::runner_stop) {
                    core_.state.command_error=runner_.command(command,core_);core_.state.command_id=command.id;
                } else if(runner_.conflicts(command,core_.state)) {
                    if(command.input){++core_.state.input_rejections;core_.state.input_error=Error::busy;core_.state.input_operation=command.op;core_.state.input_channel=command.axis;}
                    else{core_.state.command_error=Error::busy;core_.state.command_id=command.id;}
                } else core_.apply(command);
                if(!command.input) {
                    Result result{command.id,core_.state.command_error};
                    if(command.axis<kAxes && (command.op==Op::controller || command.op==Op::profile ||
                       command.op==Op::kp || command.op==Op::ki || command.op==Op::kd || command.op==Op::limit)) {
                        const auto a=command.axis;result.has_controller=true;result.axis=a;
                        result.revision=core_.state.controller_revision[a];result.profile=core_.state.profiles[a];
                        result.controller=core_.controllers[a].config;result.limit=core_.controllers[a].limit;
                    }
                    if(!results_.push(result))++lost_results_;
                }
            }
            // Stop wins over queued enables, even when the command queue was full.
            if(stop_.exchange(false)){runner_.stop(core_);core_.apply({Op::stop,0,0,false,core_.state.command_id});}
            if(link && !realtime_ready) core_.fail(Error::realtime);
            runner_.tick(core_);
            const auto values=core_.duty();
            const auto mask=core_.mask();
            uint16_t operation=0;
            std::array<char,160> error{};
            try {
                if(!mock_ && !link && begin>=next_open) {
                    next_open=begin+std::chrono::seconds(1);
                    link=std::make_unique<SpiLink>(device_.c_str());
                }
                if(mock_) {
                    core_.dsp_clear_requested=false;
                    simulated.session=1;
                    simulated.timestamp_us+=1'000'000/kRateHz;simulated.sequence++;simulated.command_sequence++;
                    simulated.encoder_valid=63;simulated.arm_mask=mask;simulated.state=core_.state.system?1:0;
                    simulated.analog_valid=63;simulated.analog_out_valid=3;
                    simulated.digital_in=simulated.digital_out=core_.state.digital_out;
                    std::copy(core_.state.analog_out.begin(),core_.state.analog_out.end(),simulated.analog_out);
                    std::copy(core_.state.analog_out.begin(),core_.state.analog_out.end(),simulated.analog_in);
                    for(unsigned a=0;a<kAxes;a++) {
                        // shortcut: deterministic first-order plant, use hardware for controller validation.
                        if(mask&(1U<<a)) {
                            if(core_.state.mode[a]==1) mock_position[a]+=values[a]*core_.controllers[a].sign*50*dt*(core_.state.position_configured[a]?core_.state.scale[a]:1000);
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
                    for(unsigned a=0;a<kAxes;a++) references[a]=static_cast<int32_t>(static_cast<uint32_t>(
                        std::llround(core_.state.zero[a]+core_.state.reference[a]*core_.state.scale[a])));
                    const auto telemetry=link->exchange(op,op==PCD_CYCLE?values:Values{},mask,references,
                        core_.state.digital_out,core_.state.analog_out);
                    for(unsigned a=0;a<kAxes;a++) if(!std::isfinite(telemetry.duty[a])||!std::isfinite(telemetry.requested_duty[a]))
                        throw std::runtime_error("Invalid numeric telemetry");
                    if(core_.dsp_clear_requested && !telemetry.fault){
                        core_.dsp_clear_requested=false;
                    }
                    if(link->session().rebooted)core_.dsp_restarted();
                    if(link->session().changed) {
                        core_.fail(Error::telemetry);++core_.state.errors;
                        std::strcpy(error.data(),"DSP session changed; resynchronizing disarmed");
                    }
                    core_.receive(telemetry,link->session().ready);
                    sent_mask=link->session().ready?mask:0;
                    sent_active=link->session().ready&&core_.state.system;

                }
            } catch(const std::exception& e) {
                std::strncpy(error.data(),e.what(),error.size()-1);
                core_.state.errors++;
                // Initial SPI pipeline fill is expected while all outputs are disabled.
                if(core_.initialized || core_.state.errors>10) core_.fail(Error::telemetry);
                core_.state.connected=false;sent_mask=0;sent_active=false;
                if(link)link->resynchronize();
            }
            const auto end=Clock::now();
            const auto work_us=std::chrono::duration<double,std::micro>(end-begin).count();
            const auto missed=missed_periods(scheduled,end);
            core_.state.max_cycle_us=std::max(core_.state.max_cycle_us,work_us);
            history.push({++iteration,static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(begin.time_since_epoch()).count()),
                work_us,late,link?link->diagnostics().duration_ns/1000.0:0,core_.state.telemetry.sequence,
                core_.state.telemetry.timestamp_us,core_.state.telemetry.command_sequence,operation,mask,commands});
            const bool new_error=error!=previous_error;
            const bool report_error=error[0] && (new_error || end-last_error_log>=std::chrono::seconds(1));
            const bool recovered=core_.state.connected && !error[0] && previous_error[0];
            if(!mock_ && (report_error || recovered || end>deadline)) {
                RealtimeEvent event;history.copy(event);event.deadline=missed!=0;event.missed_periods=missed;event.error=error;
                event.repeated_errors=repeated_errors;event.telemetry_errors=core_.state.errors;
                if(recovered)std::strcpy(event.error.data(),"SPI frames resumed; outputs remain disarmed");
                repeated_errors=0;last_error_log=end;
                if(link)event.spi=link->diagnostics();
                if(!events_.push(event)){++lost_events_;lost_periods_+=missed;}
            }
            else if(error[0])++repeated_errors;
            previous_error=error;
            if(end>deadline) {
                // Every miss is logged above; only the project policy selects automatic disarm.
                core_.deadline_missed(missed,disarm_on_deadline_miss_&&!mock_);
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

// Run with diamond-bridge stopped. Uses I/O only; never arms a motor.
#include "../dsp/host/spi.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace diamond;
struct Probe {
    int fd; PcdCommand c{};Telemetry t{};
    Probe(){
        fd=open("/dev/spidev0.0",O_RDWR);assert(fd>=0&&flock(fd,LOCK_EX|LOCK_NB)==0);
        uint8_t mode=SPI_MODE_1,bits=16;uint32_t hz=kSpiSpeedHz;
        assert(ioctl(fd,SPI_IOC_WR_MODE,&mode)==0&&ioctl(fd,SPI_IOC_WR_BITS_PER_WORD,&bits)==0&&ioctl(fd,SPI_IOC_WR_MAX_SPEED_HZ,&hz)==0);
        c.session=static_cast<uint32_t>(Clock::now().time_since_epoch().count())|1;
    }
    ~Probe(){c.operation=PCD_DISARM;for(int i=0;i<5;i++)step();close(fd);}
    void raw(const std::vector<uint16_t>& words){
        std::vector<uint16_t> rx(words.size());spi_ioc_transfer x{};
        x.tx_buf=reinterpret_cast<uintptr_t>(words.data());x.rx_buf=reinterpret_cast<uintptr_t>(rx.data());
        x.len=words.size()*2;x.speed_hz=kSpiSpeedHz;x.bits_per_word=16;
        assert(ioctl(fd,SPI_IOC_MESSAGE(1),&x)==static_cast<int>(x.len));
        if(rx.size()==PCD_WORDS){Words w{};std::copy(rx.begin(),rx.end(),w.begin());try{t=parse_telemetry(w);}catch(const InvalidTelemetryFrame&) {}}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void step(){c.sequence++;auto w=make_command(c);raw({w.begin(),w.end()});}
    void settle(uint16_t op){c.operation=op;for(int i=0;i<8;i++)step();assert(t.session==c.session&&t.command_sequence>=c.sequence-3);}
};
int main(int argc,char** argv){
    bool loopback=false,encoders=false,all_analog=false;
    for(int i=1;i<argc;i++){
        const std::string arg=argv[i];
        if(arg=="--loopback")loopback=true;
        else if(arg=="--encoders")encoders=true;
        else if(arg=="--all-analog")all_analog=true;
        else {std::cerr<<"Usage: hardware_endpoint_test [--loopback] [--encoders] [--all-analog]\n";return 2;}
    }
    if(loopback&&encoders){std::cerr<<"Digital-input and encoder tests need different wiring\n";return 2;}
    bool loopback_ok=true;
    Probe p;p.settle(PCD_SYNC);
    std::cout<<"BOOT: uptime_us="<<p.t.timestamp_us<<" encoder_valid="<<p.t.encoder_valid
             <<" cpu2_max_poll_us="<<p.t.cpu2_max_poll_us<<" fault="<<p.t.fault<<"\n";
    assert(p.t.encoder_valid==63);
    p.settle(PCD_CLEAR);p.settle(PCD_ARM);
    p.c.digital_out=10;p.c.analog_out[0]=1024;p.c.analog_out[1]=3072;p.settle(PCD_CYCLE);
    assert(p.t.arm_mask==0&&p.t.state==1&&p.t.fault==0&&p.t.digital_out==10&&p.t.analog_out[1]==3072);
    if(loopback){
        for(uint16_t bits=0;bits<16;bits++){
            p.c.digital_out=bits;p.settle(PCD_CYCLE);
            std::cout<<"DIO: commanded="<<bits<<" measured="<<p.t.digital_in<<"\n";
            loopback_ok &= p.t.digital_in==bits;
        }
        std::cout<<(loopback_ok?"PASS":"FAIL")<<": four digital loopbacks\n";
    }
    if(loopback||all_analog){
        bool analog_ok=true;
        for(uint16_t value:{512,2048,3584}){
            p.c.analog_out[0]=value;p.c.analog_out[1]=4095-value;p.settle(PCD_CYCLE);
            std::cout<<"AIO: DAC="<<value<<","<<4095-value<<" ADC=";
            for(unsigned a=0;a<(all_analog?6U:2U);a++){
                std::cout<<(a?",":"")<<p.t.analog_in[a];
                analog_ok &= std::abs(int(p.t.analog_in[a])-int(p.c.analog_out[a%2]))<100;
            }
            std::cout<<"\n";analog_ok &= p.t.analog_valid==63;
        }
        loopback_ok &= analog_ok;
        std::cout<<(analog_ok?"PASS":"FAIL")<<": "<<(all_analog?6:2)<<" analog input loopbacks\n";
    }
    if(encoders){
        bool encoder_ok=true;
        // DO0/1/2 drive separate A inputs; DO3 drives all three B inputs.
        // Other channels see B-only excursions and must return to their initial counts.
        p.c.digital_out=0;p.settle(PCD_CYCLE);
        const auto initial_errors=p.t.encoder_errors;
        for(uint16_t a_mask:{1,2,4,7})for(int direction:{1,-1}){
            const std::array<int32_t,3> before={p.t.position[3],p.t.position[4],p.t.position[5]};
            const auto started=p.t.timestamp_us;
            const unsigned cycles=128;
            const std::array<uint16_t,4> phases=direction==1?
                std::array<uint16_t,4>{a_mask,uint16_t(a_mask|8),8,0}:
                std::array<uint16_t,4>{8,uint16_t(a_mask|8),a_mask,0};
            for(unsigned cycle=0;cycle<cycles;cycle++)for(auto phase:phases){p.c.digital_out=phase;p.step();}
            p.settle(PCD_CYCLE);
            std::cout<<"ENCODERS: A_mask="<<a_mask<<" direction="<<direction<<" elapsed_us="
                     <<uint32_t(p.t.timestamp_us-started)<<" count_deltas=";
            for(unsigned a=0;a<3;a++){
                const auto delta=int32_t(uint32_t(p.t.position[a+3])-uint32_t(before[a]));
                const auto expected=(a_mask&(1U<<a))?direction*int(cycles*4):0;
                std::cout<<(a?",":"")<<delta;
                encoder_ok &= delta==expected;
            }
            std::cout<<"\n";
            encoder_ok &= p.t.encoder_valid==63&&p.t.encoder_errors==initial_errors;
        }
        loopback_ok &= encoder_ok;
        std::cout<<"ENCODER HEALTH: valid="<<p.t.encoder_valid
                 <<" new_transition_errors="<<uint32_t(p.t.encoder_errors-initial_errors)
                 <<" cpu2_max_poll_us="<<p.t.cpu2_max_poll_us<<"\n";
        std::cout<<(encoder_ok?"PASS":"FAIL")<<": CPU2 counts, independent channels, forward/reverse and simultaneous edges\n";
    }
    auto invalid=p.t.invalid_frames;auto bad=make_command(p.c);bad[95]^=1;
    for(int i=0;i<6;i++)p.raw({bad.begin(),bad.end()});
    assert(p.t.invalid_frames>invalid&&p.t.fault==0);p.settle(PCD_CYCLE);
    std::cout<<"PASS: CRC rejection and recovery\n";
    // A stopped DMA transfer must not carry its remaining count into the next CS.
    invalid=p.t.invalid_frames;
    p.c.sequence++;auto full=make_command(p.c);
    p.raw(std::vector<uint16_t>(full.begin(),full.begin()+48));p.settle(PCD_CYCLE);
    assert(p.t.invalid_frames>invalid);
    for(unsigned length:{97,112}){
        invalid=p.t.invalid_frames;p.c.sequence++;full=make_command(p.c);
        auto long_frame=std::vector<uint16_t>(full.begin(),full.end());long_frame.resize(length,0);
        p.raw(long_frame);p.settle(PCD_CYCLE);assert(p.t.invalid_frames>invalid);
    }
    std::cout<<"PASS: short/long frame rejection and recovery\n";
    auto duplicate=make_command(p.c);auto before=p.t.watchdogs;
    for(int i=0;i<70;i++)p.raw({duplicate.begin(),duplicate.end()});
    assert(p.t.fault&PCD_FAULT_TIMEOUT);assert(p.t.watchdogs==before+1&&p.t.arm_mask==0&&p.t.digital_out==0&&p.t.analog_out[0]==0&&p.t.analog_out[1]==0);
    p.c.operation=PCD_ARM;for(int i=0;i<6;i++)p.step();assert(p.t.fault&&p.t.state==2);
    p.settle(PCD_CLEAR);assert(p.t.fault==0&&p.t.state==0);
    p.settle(PCD_ARM);p.settle(PCD_CYCLE);assert(p.t.state==1);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    for(int i=0;i<5;i++)p.step();
    assert(p.t.fault&PCD_FAULT_TIMEOUT);
    p.settle(PCD_CLEAR);p.settle(PCD_DISARM);
    std::cout<<"PASS: stale command / silent host watchdog, zero I/O, explicit fault clear, disarmed finish\n";
    return loopback_ok?0:1;
}

#include "../dsp/common/endpoint.h"
#include "../dsp/common/quadrature.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
uint16_t reference_crc(const uint16_t *w,unsigned n){
    uint16_t crc=0xffff;
    for(unsigned i=0;i<n;i++)for(int b=15;b>=0;b--){
        bool feedback=((crc>>15)^((w[i]>>b)&1))!=0;
        crc=static_cast<uint16_t>(crc<<1);if(feedback)crc^=0x1021;
    }
    return crc;
}
int main(){
    static_assert(sizeof(float)==4 && sizeof(uint32_t)==4);
    std::array<uint16_t,PCD_WORDS> w{},bad;
    PcdCommand c{},decoded{};c.operation=PCD_CYCLE;c.session=0x12345678;c.sequence=0xabcdef01;
    c.arm_mask=3;c.digital_out=10;c.analog_out[0]=4095;c.analog_out[1]=2048;
    c.duty[0]=.25f;c.duty[1]=-.5f;c.reference[0]=-12345;c.reference[1]=INT32_MAX;
    pcd_encode_command(w.data(),&c);
    assert(w[0]==0x55aa&&w[1]==5&&w[2]==96&&w[4]==0x5678&&w[5]==0x1234);
    assert(w[12]==0&&w[13]==0x3e80&&w[14]==0&&w[15]==0xbf00);
    assert(w[95]==reference_crc(w.data(),95));
    assert(pcd_decode_command(w.data(),&decoded)&&decoded.reference[0]==-12345&&decoded.duty[1]==-.5f);
    for(unsigned i=0;i<96;i++)for(unsigned bit=0;bit<16;bit++){
        bad=w;bad[i]^=1U<<bit;assert(!pcd_decode_command(bad.data(),&decoded));
    }
    bad=w;bad[36]=1;bad[95]=pcd_crc(bad.data(),95);assert(!pcd_decode_command(bad.data(),&decoded));
    PcdTelemetry t{},read{};t.timestamp_us=0xfffffffe;t.sequence=123;t.session=c.session;t.command_sequence=c.sequence;
    t.position[0]=-987654;t.requested_duty[1]=-.25f;t.duty[0]=.5f;t.reference[0]=123456;
    t.analog_in[0]=4095;t.analog_out[1]=1234;t.encoder_valid=63;t.fault=1;t.watchdogs=7;
    pcd_encode_telemetry(w.data(),&t);
    assert(w[92]==reference_crc(w.data()+4,88));assert(pcd_decode_telemetry(w.data(),&read));
    assert(read.position[0]==t.position[0]&&read.duty[0]==.5f&&read.reference[0]==123456);
    assert(read.analog_out[1]==1234&&read.watchdogs==7&&read.timestamp_us==0xfffffffe);
    for(unsigned i=4;i<=92;i++){bad=w;bad[i]^=1;assert(!pcd_decode_telemetry(bad.data(),&read));}

    PcdEndpoint e{};c={};c.session=1;c.sequence=1;c.operation=PCD_ARM;c.arm_mask=3;
    assert(!pcd_accept(&e,&c,0,63)&&!e.active);
    c.operation=PCD_SYNC;assert(pcd_accept(&e,&c,0,63)&&!e.active);
    c.operation=PCD_ARM;c.sequence++;assert(pcd_accept(&e,&c,10,63)&&e.mask==3&&e.active);
    c.operation=PCD_CYCLE;c.sequence++;c.duty[0]=.25f;c.digital_out=15;c.analog_out[0]=2048;
    assert(pcd_accept(&e,&c,20,63));assert(e.command.duty[0]==.25f);
    assert(!pcd_accept(&e,&c,30,63)&&e.last_update_us==20); // duplicate is not a heartbeat
    PcdEndpoint replay=e;PcdCommand stale=c;stale.operation=PCD_SYNC;stale.sequence=1;
    assert(!pcd_accept(&replay,&stale,31,63)&&!replay.active&&replay.command.sequence==c.sequence);
    stale.operation=PCD_ARM;stale.sequence=2;
    assert(!pcd_accept(&replay,&stale,32,63)&&!replay.active);
    pcd_tick(&e,50021,63);assert(e.fault==PCD_FAULT_TIMEOUT&&!e.mask&&!e.active&&e.command.digital_out==0&&e.command.analog_out[0]==0);
    c.sequence++;c.operation=PCD_ARM;assert(!pcd_accept(&e,&c,50030,63)&&!e.mask);
    c.sequence++;c.operation=PCD_CLEAR;assert(pcd_accept(&e,&c,50040,63)&&!e.fault&&!e.active);
    c.sequence++;c.operation=PCD_ARM;assert(pcd_accept(&e,&c,50050,63));
    c.sequence++;c.operation=PCD_CYCLE;c.duty[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!pcd_accept(&e,&c,50060,63)&&e.fault==PCD_FAULT_COMMAND&&!e.mask);
    c={};c.session=2;c.sequence=1;c.operation=PCD_SYNC;assert(pcd_accept(&e,&c,0xffffff00,63)&&e.fault);
    c.sequence++;c.operation=PCD_CLEAR;assert(pcd_accept(&e,&c,0xffffff01,63)&&!e.fault);
    c.sequence++;c.operation=PCD_ARM;c.arm_mask=32;assert(pcd_accept(&e,&c,0xffffff02,63));
    pcd_tick(&e,10,63);assert(!e.fault);pcd_tick(&e,20,7);assert(e.fault==PCD_FAULT_ENCODER&&!e.mask);
    int position=0;const unsigned forward[]={0,1,3,2,0};
    for(int i=1;i<5;i++)position+=pcd_quadrature_step(forward[i-1],forward[i]);
    assert(position==4);for(int i=4;i>0;i--)position+=pcd_quadrature_step(forward[i],forward[i-1]);
    assert(position==0&&pcd_quadrature_step(0,3)==2&&pcd_quadrature_step(2,2)==0);
    std::cout<<"PASS: v5 wire packing, independent CRC, bit corruption, reserved words, endpoint state/watchdog/wrap, x4 quadrature\n";
}

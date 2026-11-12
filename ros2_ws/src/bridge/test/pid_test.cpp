#include "bridge/controllers.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace diamond;
constexpr double dt=.0005;
void near(double actual,double expected) {assert(std::abs(actual-expected)<1e-10);}
int main() {
    PidController p;p.tuning={.001,0,0,1,1};
    for(int i=0;i<20000;i++)near(p.step(2000,dt),1);
    near(p.step(1000,dt),1);near(p.step(500,dt),.5);
    near(p.step(0,dt),0);near(p.step(-2000,dt),-1);near(p.step(0,dt),0);
    near(p.integral,0);

    const double scale=(500.0*4*(91.0/6))/(30*std::acos(-1));
    AxisController axis;axis.sign=1;
    ControllerConfig config;config.parameters={.0005,0,.0001};
    axis.configure(config,false);near(axis.step(0,0,scale,dt),0);
    near(axis.step(1,0,scale,dt),1); // Derivative kick saturates this tick only.
    for(int i=0;i<20000;i++)near(axis.step(1,0,scale,dt),.0005*scale);
    near(axis.step(0,0,scale,dt),-1);near(axis.step(0,0,scale,dt),0);

    PidController live;live.tuning={0,0,.0001,1,1};
    near(live.step(0,dt),0);near(live.step(1,dt),.2);
    auto next=live.tuning;next.kd=0;live.retune(next,true);
    for(int i=0;i<20000;i++)near(live.step(1,dt),0);
    next.kp=.1;live.retune(next,true);near(live.step(1,dt),.1);

    PidController pi;pi.tuning={.001,.1,0,1,1};
    for(int i=0;i<20000;i++)near(pi.step(2000,dt),1);
    near(pi.integral,0); // No windup or artificial counteracting bias.
    near(pi.step(10,dt),.01);near(pi.integral,.0005);
    near(pi.step(-10,dt),-.0095);near(pi.integral,0);
    pi.integral=2;pi.step(-10,dt);assert(pi.integral<2); // Unwind while saturated.
    next=pi.tuning;next.ki=0;pi.retune(next,true);
    near(pi.step(0,dt),0);near(pi.integral,0);

    PidController pid;pid.tuning={.01,.02,.001,1,100};
    near(pid.step(10,dt),.1);near(pid.step(12,dt),4.1201);
    pid.reset();near(pid.step(10,dt),.1); // No derivative kick on initialization.
    pid.tuning.sign=-1;pid.reset();near(pid.step(10,dt),-.1);

    PidController filtered;filtered.tuning={0,0,.0001,1,1,50};
    near(filtered.step(0,dt),0);
    double d=filtered.step(1,dt),area=d*dt;
    assert(d>0&&d<.03); // Raw one-count derivative would produce .2 duty.
    for(int i=0;i<2000;i++) {
        const auto previous=d;d=filtered.step(1,dt);
        assert(d>=0&&d<=previous);area+=d*dt;
    }
    near(d,0);near(area,.0001); // Attenuate the peak without changing integrated D response.
    filtered.reset();
    for(int i=0;i<1000;i++)d=filtered.step(i,dt);
    near(d,.2); // Constant velocity retains the derivative's DC gain.
    auto filter_tuning=filtered.tuning;filter_tuning.d_filter_hz=5;
    filtered.retune(filter_tuning,true);near(filtered.step(1000,dt),.2);
    filter_tuning.d_filter_hz=0;filtered.retune(filter_tuning,true);
    near(filtered.step(1000,dt),0);near(filtered.step(1001,dt),.2);
    filter_tuning.d_filter_hz=50;filtered.retune(filter_tuning,true);filtered.reset();
    near(filtered.step(500,dt),0);

    PidController plain_pi,filtered_pi;
    plain_pi.tuning={.001,.01,0,1,1,0};filtered_pi.tuning={.001,.01,0,1,1,50};
    for(int i=0;i<2000;i++)near(plain_pi.step(20*std::sin(i*.01),dt),filtered_pi.step(20*std::sin(i*.01),dt));
    config.parameters[3]=50;assert(valid_controller(config));
    config.parameters[3]=-1;assert(!valid_controller(config));
    config.parameters[3]=1001;assert(!valid_controller(config));
    config.parameters[3]=NAN;assert(!valid_controller(config));
    std::cout<<"PASS: P/PD saturation, tuning, anti-windup, D units, low-pass/bypass/reset, live cutoff and unchanged PI\n";
}

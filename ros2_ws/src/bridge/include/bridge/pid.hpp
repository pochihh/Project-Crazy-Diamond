#pragma once
#include <algorithm>
#include <cmath>
namespace diamond {
struct Tuning { double kp=0.0005, ki=0.001, kd=0, sign=-1, limit=1; };
struct PidController {
    Tuning tuning{};
    double integral=0, previous=0, previous_derivative=0;
    bool have_previous=false;
    void reset() { integral=previous=previous_derivative=0; have_previous=false; }
    void retune(const Tuning& next, bool running) {
        // Bumpless updates use I only when integral action is enabled.
        if(!running || !have_previous) reset();
        else if(next.ki>0)
            integral+=(tuning.kp-next.kp)*previous+(tuning.kd-next.kd)*previous_derivative;
        else integral=0;
        tuning=next;
    }
    double step(double error, double dt) {
        if(tuning.ki==0) integral=0;
        const double derivative=have_previous ? (error-previous)/dt : 0;
        const double raw=tuning.kp*error+integral+tuning.kd*derivative;
        if (!std::isfinite(raw)) return NAN;
        const double output=std::clamp(raw,-tuning.limit,tuning.limit);
        const double increment=tuning.ki*error*dt;
        // Conditional integration: saturation must not manufacture an I contribution.
        if((raw<tuning.limit || increment<0) && (raw>-tuning.limit || increment>0))
            integral+=increment;
        previous=error; previous_derivative=derivative; have_previous=true;
        return tuning.sign*output;
    }
};
}

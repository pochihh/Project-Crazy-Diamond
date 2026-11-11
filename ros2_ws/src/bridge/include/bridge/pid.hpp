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
        // Integral is stored as output contribution; rebase P/D without losing I history.
        if(running && have_previous)
            integral+=(tuning.kp-next.kp)*previous+(tuning.kd-next.kd)*previous_derivative;
        else reset();
        tuning=next;
    }
    double step(double error, double dt) {
        const double derivative=have_previous ? (error-previous)/dt : 0;
        const double raw=tuning.kp*error+integral+tuning.kd*derivative;
        if (!std::isfinite(raw)) return NAN;
        const double output=std::clamp(raw,-tuning.limit,tuning.limit);
        integral += tuning.ki*error*dt+(output-raw);
        previous=error; previous_derivative=derivative; have_previous=true;
        return tuning.sign*output;
    }
};
}

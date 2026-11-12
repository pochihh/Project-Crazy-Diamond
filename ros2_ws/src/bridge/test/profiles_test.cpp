#include "bridge/profiles.hpp"
#include <cassert>
#include <iostream>
using namespace diamond;
template<class F> void rejected(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}assert(failed);}
int main() {
    const auto dir=std::filesystem::temp_directory_path()/("pcd-profiles-"+std::to_string(getpid()));
    std::filesystem::remove_all(dir);Profiles profiles(dir);
    ControllerConfig c;c.slew_enabled=true;c.slew_rate_mm_s=123.45;c.parameters[3]=50;
    profiles.save("shared",c,true);
    const auto first=profiles.load("shared");assert(first.controller.parameters==c.parameters);
    assert(first.controller.slew_enabled&&first.controller.slew_rate_mm_s==123.45);
    rejected([&]{profiles.save("shared",c,true);});
    c.parameters[0]=0.0006;profiles.save("shared",c,false);
    assert(profiles.load("shared").controller.parameters[0]==0.0006);
    assert(first.controller.parameters[0]==0.0005); // Existing runtime copy is independent.
    rejected([&]{profiles.load("../shared");});
    rejected([&]{profiles.save("missing",c,false);});
    rejected([&]{Profiles::parameters("lqr",{},{});});
    rejected([&]{Profiles::parameters("pid",{"kp","kp","kd","d_filter_hz"},{1,2,3,50});});
    rejected([&]{Profiles::parameters("pid",{"kp","ki","kd","d_filter_hz"},{1,NAN,3,50});});
    rejected([&]{Profiles::parameters("pid",{"kp"},{1});});
    rejected([&]{Profiles::parameters("pid",{"kp","ki","kd"},{1,2,3});}); // API updates remain complete.
    std::ofstream(dir/"legacy.yaml")<<"schema_version: 2\nid: legacy\ntype: pid\nsample_period_s: 0.0005\nparameters: {kp: 0.001, ki: 0, kd: 0.00001}\nreference: {type: passthrough, max_rate_mm_s: 20}\n";
    const auto legacy=profiles.load("legacy");assert(legacy.controller.parameters[3]==0);
    profiles.save("legacy",legacy.controller,false);
    assert(YAML::LoadFile((dir/"legacy.yaml").string())["parameters"]["d_filter_hz"].as<double>()==0);
    std::ofstream(dir/"invalid.yaml")<<"schema_version: 1\nid: invalid\ntype: pid\nsample_period_s: 0.001\nparameters: {kp: 1, ki: 1, kd: 0}\n";
    rejected([&]{profiles.load("invalid");});
    std::filesystem::remove_all(dir);
    std::cout<<"PASS: profile round-trip, explicit copies, atomic replace/create, schema/parameter validation\n";
}

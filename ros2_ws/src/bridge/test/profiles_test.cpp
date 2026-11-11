#include "bridge/profiles.hpp"
#include <cassert>
#include <iostream>
using namespace diamond;
template<class F> void rejected(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}assert(failed);}
int main() {
    const auto dir=std::filesystem::temp_directory_path()/("pcd-profiles-"+std::to_string(getpid()));
    std::filesystem::remove_all(dir);Profiles profiles(dir);
    ControllerConfig c;
    profiles.save("shared",c,true);
    const auto first=profiles.load("shared");assert(first.controller.parameters==c.parameters);
    rejected([&]{profiles.save("shared",c,true);});
    c.parameters[0]=0.0006;profiles.save("shared",c,false);
    assert(profiles.load("shared").controller.parameters[0]==0.0006);
    assert(first.controller.parameters[0]==0.0005); // Existing runtime copy is independent.
    rejected([&]{profiles.load("../shared");});
    rejected([&]{profiles.save("missing",c,false);});
    rejected([&]{Profiles::parameters("lqr",{},{});});
    rejected([&]{Profiles::parameters("pid",{"kp","kp","kd"},{1,2,3});});
    rejected([&]{Profiles::parameters("pid",{"kp","ki","kd"},{1,NAN,3});});
    rejected([&]{Profiles::parameters("pid",{"kp"},{1});});
    std::ofstream(dir/"invalid.yaml")<<"schema_version: 1\nid: invalid\ntype: pid\nsample_period_s: 0.001\nparameters: {kp: 1, ki: 1, kd: 0}\n";
    rejected([&]{profiles.load("invalid");});
    std::filesystem::remove_all(dir);
    std::cout<<"PASS: profile round-trip, explicit copies, atomic replace/create, schema/parameter validation\n";
}

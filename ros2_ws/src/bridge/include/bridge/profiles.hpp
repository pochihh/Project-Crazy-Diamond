#pragma once
#include "controllers.hpp"
#include "spi.hpp"
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace diamond {
struct Profile { ProfileId id{}; ControllerConfig controller{}; };
class Profiles {
    std::filesystem::path directory_;
    std::filesystem::path path(const std::string& id) const {profile_id(id);return directory_/(id+".yaml");}
public:
    explicit Profiles(std::filesystem::path directory):directory_(std::move(directory)){std::filesystem::create_directories(directory_);}
    static ControllerConfig parameters(const std::string& type,const std::vector<std::string>& names,const std::vector<double>& values) {
        ControllerConfig c;c.type=controller_index(type);c.parameters.fill(0);
        const auto& desc=controller_types[c.type];
        if(names.size()!=desc.count||values.size()!=desc.count)throw std::runtime_error("Supply the complete controller parameter set");
        std::array<bool,max_controller_parameters> found{};
        for(unsigned i=0;i<names.size();i++) {
            unsigned j=0;while(j<desc.count&&names[i]!=desc.parameters[j].name)j++;
            if(j==desc.count||found[j])throw std::runtime_error("Unknown or duplicate controller parameter: "+names[i]);
            found[j]=true;c.parameters[j]=values[i];
        }
        if(!valid_controller(c))throw std::runtime_error("Controller parameters are non-finite or out of range");
        return c;
    }
    Profile load(const std::string& id) const {
        const auto node=YAML::LoadFile(path(id).string());
        if(!node.IsMap())throw std::runtime_error("Profile must be a mapping");
        for(const auto& entry:node) {
            const auto key=entry.first.as<std::string>();
            if(key!="schema_version"&&key!="id"&&key!="type"&&key!="sample_period_s"&&key!="parameters")
                throw std::runtime_error("Unknown profile field: "+key);
        }
        if(node["schema_version"].as<unsigned>()!=1||node["id"].as<std::string>()!=id)
            throw std::runtime_error("Profile schema or ID does not match: "+id);
        if(node["sample_period_s"].as<double>()!=1.0/kRateHz)throw std::runtime_error("Profile sample period does not match the control loop");
        const auto params=node["parameters"];
        if(!params.IsMap())throw std::runtime_error("Profile parameters must be a mapping");
        std::vector<std::string> names;std::vector<double> values;
        for(const auto& item:params){names.push_back(item.first.as<std::string>());values.push_back(item.second.as<double>());}
        return {profile_id(id),parameters(node["type"].as<std::string>(),names,values)};
    }
    std::vector<std::string> ids() const {
        std::vector<std::string> out;
        for(const auto& entry:std::filesystem::directory_iterator(directory_))
            if(entry.is_regular_file()&&entry.path().extension()==".yaml")out.push_back(entry.path().stem().string());
        std::sort(out.begin(),out.end());return out;
    }
    void save(const std::string& id,const ControllerConfig& c,bool create) const {
        const auto destination=path(id);
        if(!valid_controller(c))throw std::runtime_error("Invalid controller parameters");
        if(!create&&!std::filesystem::exists(destination))throw std::runtime_error("Profile is missing; use Save as");
        YAML::Node node;node["schema_version"]=1;node["id"]=id;node["type"]=controller_types[c.type].id;
        node["sample_period_s"]=1.0/kRateHz;
        for(unsigned i=0;i<controller_types[c.type].count;i++)node["parameters"][controller_types[c.type].parameters[i].name]=c.parameters[i];
        const auto temporary=destination.string()+"."+std::to_string(getpid())+".tmp";
        try {
            std::ofstream file(temporary);file<<node<<'\n';file.close();
            if(!file)throw std::runtime_error("Cannot write controller profile");
            if(create) {std::filesystem::create_hard_link(temporary,destination);std::filesystem::remove(temporary);}
            else std::filesystem::rename(temporary,destination);
        } catch(...) {std::filesystem::remove(temporary);throw;}
    }
};
}

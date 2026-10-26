#pragma once
#include "script/runtime.hpp"
#include "assets/resources.hpp"
#include <array>

namespace lifeline::script {
class Inventory {
public:
    struct Record {
        // Original integer columns 0,1,2,3,6,7; string offsets are replaced
        // by owned UTF-8 strings. Quantity is original column 2 (selector 3).
        std::array<std::int32_t,6> fields{};
        std::string label,description;
        int quantity()const{return fields[2];}
    };
private:
    Resources& resources_;
    std::array<std::map<int,Record>,2> banks_;
public:
    explicit Inventory(Resources& resources):resources_(resources){}
    void select(unsigned bank,const std::string& member);
    const Record* record(unsigned bank,int id)const;
    const auto& records(unsigned bank)const{return banks_.at(bank);}
    Bindings bindings();
    void speech_attempt();
    void speech_score(unsigned score);
    Bytes snapshot()const;
    static std::int32_t snapshot_value(std::span<const std::uint8_t>,int id,int selector);
    void restore(std::span<const std::uint8_t>);
};
}

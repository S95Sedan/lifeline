#pragma once
#include "script/runtime.hpp"
#include <cmath>
#include <array>
#include <stdexcept>

namespace lifeline::script {
class Input {
    bool enabled_=true;
    std::uint16_t buttons_=0;
    std::uint16_t requested_presses_=0;
    std::array<std::uint32_t,2> blocked_{};
    std::array<std::uint16_t,2> current_{};
    std::array<std::array<std::uint32_t,16>,2> held_frames_{};
    unsigned layout_=0;
    static constexpr std::array<std::array<unsigned,16>,3> layouts_{{
        {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
        {0,1,2,3,4,5,6,7,9,11,10,13,12,15,14,8},
        {0,1,2,3,4,5,6,7,9,11,10,13,12,14,15,8}}};
public:
    void buttons(std::uint16_t held){buttons_=held;}
    void consume(std::uint16_t mask){buttons_&=~mask;}
    void clear_requests(){requested_presses_=0;}
    // Original pad packet bytes are active-low. 1e9558 ORs these locks
    // into the packet after exchanging the mask's low/high bytes.
    std::uint16_t allowed(unsigned port=0)const{
        const auto mask=blocked_.at(port);
        return enabled_?std::uint16_t(~(((mask>>8)&255)|((mask&255)<<8))):0;
    }
    std::uint16_t mapped_buttons()const{
        std::uint16_t mapped=0;
        for(unsigned bit=0;bit<16;++bit)if(buttons_&(1u<<bit))mapped|=std::uint16_t(1u<<layouts_[layout_][bit]);
        return mapped;
    }
    std::uint16_t buttons()const{return mapped_buttons()&allowed();}
    bool enabled()const{return enabled_;}
    std::uint16_t requested_presses()const{return requested_presses_&allowed();}
    void advance(){
        const auto held=buttons();
        requested_presses_&=~held;
        current_[0]=std::uint16_t((held>>8)|(held<<8));
        current_[1]=0; // The original game masks the second controller packet.
        for(unsigned port=0;port<2;++port)for(unsigned bit=0;bit<16;++bit){
            auto& count=held_frames_[port][bit];count=(current_[port]&(1u<<bit))?count+1:0;
        }
    }
    Bindings bindings(){
        Bindings result;
        result.emplace(163,Binding{1,0,[this](const Invocation& c){
            const auto value=c.arguments[0];if(!std::isfinite(value) || std::trunc(value)<0 || std::trunc(value)>2)throw std::runtime_error("Invalid controller layout");
            layout_=unsigned(std::trunc(value));return Reply{false,0.f};
        }});
        result.emplace(168,Binding{0,0,[this](const Invocation&){return Reply{false,float(layout_)};}});
        for(unsigned op:{123u,124u})result.emplace(op,Binding{0,3,[this,op](const Invocation& c){
            if(!c.state)throw std::logic_error("Button query requires worker locals");
            const auto port=c.operands[0],bit=c.operands[1];
            if(port>=2 || bit>=16)throw std::runtime_error("Button query outside original pad bank");
            const auto mask=current_[port]&(1u<<bit);
            if(op==123&&port==0&&!mask)requested_presses_|=std::uint16_t(1u<<((bit+8)%16));
            c.state->integers[c.operands[2]]=op==123?(mask && held_frames_[port][bit]==1):mask;
            return Reply{};
        }});
        result.merge(Bindings{{165,Binding{1,0,[this](const Invocation& c){
            if(!std::isfinite(c.arguments[0]))throw std::runtime_error("Invalid input enable value");
            enabled_=std::trunc(c.arguments[0])!=0;return Reply{false,0.f};
        }}},{166,Binding{3,0,[this](const Invocation& c){
            for(auto value:c.arguments)if(!std::isfinite(value) || double(value)<-2147483648.0 || double(value)>=2147483648.0)throw std::runtime_error("Invalid button lock argument");
            const auto port=int(c.arguments[0]);
            if(port<0 || port>=2)throw std::runtime_error("Button lock port outside controller bank");
            const auto bit=std::uint32_t(int(c.arguments[1]))&31;
            const auto mask=std::uint32_t(1)<<bit;
            if(int(c.arguments[2]))blocked_[port]|=mask;else blocked_[port]&=~mask;
            return Reply{false,0.f};
        }}}});
        return result;
    }
};
}

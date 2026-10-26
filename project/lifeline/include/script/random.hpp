#pragma once
#include "script/runtime.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace lifeline::script {
constexpr std::uint32_t cd_clock_seed(unsigned second,unsigned minute){
    const auto bcd=[](unsigned value){return ((value/10)<<4)|(value%10);};
    return bcd(second)*bcd(minute);
}
// Registry 8010: MT recurrence with the game's older 69069 seed expansion.
// This stream is independent of the libc LCG used by visual effects.
class Random {
    std::array<std::uint32_t,624> state_{};
    unsigned cursor_=624;
public:
    explicit Random(std::uint32_t value=4357){seed(value);}
    void seed(std::uint32_t value){
        for(auto& word:state_){const auto high=value&0xffff0000u;value=value*69069u+1;word=high|(value>>16);value=value*69069u+1;}
        cursor_=624;
    }
    std::uint32_t next(){
        if(cursor_==624){
            for(unsigned i=0;i<624;++i){
                const auto joined=(state_[i]&0x80000000u)|(state_[(i+1)%624]&0x7fffffffu);
                state_[i]=state_[(i+397)%624]^(joined>>1)^((joined&1)?0x9908b0dfu:0u);
            }
            cursor_=0;
        }
        auto value=state_[cursor_++];value^=value>>11;value^=(value<<7)&0x9d2c5680u;value^=(value<<15)&0xefc60000u;return value^(value>>18);
    }
    Bindings bindings(){
        return {{67,Binding{0,2,[this](const Invocation& c){
            if(!c.state)throw std::runtime_error("Random assignment requires script state");
            const auto divisor=c.operands[1];if(!divisor)throw std::runtime_error("Random modulus is zero");
            c.state->integers[c.operands[0]]=std::bit_cast<std::int32_t>(next()%divisor);return Reply{};
        }}},{68,Binding{1,0,[this](const Invocation& c){
            const auto n=c.arguments[0];if(!std::isfinite(n)||double(n)<-2147483648.0||double(n)>=2147483648.0)throw std::runtime_error("Invalid random modulus");
            const auto divisor=std::uint32_t(std::int32_t(n));if(!divisor)throw std::runtime_error("Random modulus is zero");
            const auto value=std::rotl(next(),16)%divisor;
            return Reply{false,float(std::bit_cast<std::int32_t>(value))};
        }}}};
    }
};
}

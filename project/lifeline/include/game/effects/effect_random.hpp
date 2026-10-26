#pragma once
#include <cstdint>
#include <functional>
#include <utility>

namespace lifeline::script {
// 1d5560/1d5638: visual-effect random stream. Native platform time supplies
// entropy where the original sampled the two console hardware timers.
class EffectRandom {
    std::function<std::pair<std::uint32_t,std::uint32_t>()> timers_;
    std::uint32_t value_=0,multiplier_=0,increment_=0,calls_=0;
    void seed(std::uint32_t a,std::uint32_t b){multiplier_=((a&0x7fe)|2)*5-1;increment_=(b&0x7ff)|1;value_=a&0xfff;}
public:
    explicit EffectRandom(std::function<std::pair<std::uint32_t,std::uint32_t>()> timers):timers_(std::move(timers)){
        const auto [a,b]=timers_();seed(a,b);
    }
    std::uint32_t next(){
        if(calls_++>=201){
            const auto previous=value_*multiplier_+increment_;const auto [a,b]=timers_();
            calls_=0;seed(a+previous,b+~previous);
        }
        value_=(value_*multiplier_+increment_)&0xfff;return value_;
    }
};
}

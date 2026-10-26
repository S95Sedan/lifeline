#include "ui/pause.hpp"
#include <cmath>
#include <stdexcept>
#include <algorithm>
namespace lifeline {
script::Bindings Pause::bindings(){return {{164,script::Binding{1,0,[this](const script::Invocation& c){
    const float v=std::trunc(c.arguments[0]);if(!std::isfinite(v)||double(v)<INT32_MIN||double(v)>INT32_MAX)throw std::runtime_error("Invalid pause permission");enabled_=int(v);return script::Reply{false,0};
}}}};}
void Pause::advance(std::uint16_t held){
    const auto pressed=std::uint16_t(held&~previous_);previous_=held;
    if(resume_settings_){resume_settings_=false;active_=false;state_=selection_=0;previous_=0;return;}
    if(!active_){if(enabled_&&(pressed&8)){active_=true;state_=selection_=0;confirmation_=1;timer_=0;}return;}
    timer_+=16.67f;if(timer_>500)timer_=0;
    if(state_==2)return; // The PC settings panel owns input until it closes.
    if(pressed&(0x2000|8)){if(state_==1)state_=0;else active_=false;return;}
    if(state_==0){
        if(pressed&0x10){selection_=std::max(0,selection_-1);return;}if(pressed&0x40){selection_=std::min(2,selection_+1);return;}
        if(!(held&8)&&(pressed&0x8000)){if(selection_==0)active_=false;else if(selection_==1)state_=2;else {state_=1;confirmation_=1;}}
    }else{
        if(pressed&0x80)confirmation_=0;if(pressed&0x20)confirmation_=1;
        if(!(held&8)&&(pressed&0x8000)){if(confirmation_==1)state_=0;else {active_=false;reset_=true;}}
    }
}
float Pause::pulse()const{return std::sin(timer_*.001f*6.28318530718f);}
}

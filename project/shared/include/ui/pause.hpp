#pragma once
#include "script/runtime.hpp"
namespace lifeline {
class Pause {
    int enabled_=1,state_=0,selection_=0,confirmation_=1;
    bool active_=false,reset_=false,resume_settings_=false;
    std::uint16_t previous_=0;
    float timer_=0;
public:
    // The kernel restores permission after returning from Data Load / Save.
    void enable(bool value){enabled_=value;}
    script::Bindings bindings();
    void advance(std::uint16_t held);
    bool active()const{return active_;}
    bool settings_requested()const{return active_&&state_==2;}
    void finish_settings(){if(settings_requested())resume_settings_=true;}
    bool reset_requested()const{return reset_;}
    int state()const{return state_;}
    int selection()const{return selection_;}
    int confirmation()const{return confirmation_;}
    float pulse()const;
};
}

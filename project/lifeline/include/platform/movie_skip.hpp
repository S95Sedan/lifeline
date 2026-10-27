#pragma once
#include "platform/bindings.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
namespace lifeline {
// One continuous Confirm hold belongs to one visible movie section. The host
// supplies its generation and monotonic milliseconds (not simulation time).
class MovieSkipHold {
    BindingState bindings_;
    std::uint64_t movie_=0;
    std::optional<std::uint64_t> started_;
    bool blocked_=false,eligible_=false,focused_=true,owns_input_=false;
    SDL_JoystickID selected_=0;
    InputBinding keyboard_=input_bindings().keyboard[unsigned(InputAction::confirm)],controller_=input_bindings().joystick[unsigned(InputAction::confirm)];
    bool held()const{return bindings_.held(InputAction::confirm,0)||bindings_.held(InputAction::confirm,1);}
public:
    static constexpr std::uint64_t duration_ms=5000;
    void movie(std::uint64_t generation,bool eligible,std::uint64_t now,bool owns_input=true){
        const bool owns=generation&&owns_input;
        if(generation!=movie_||(eligible_&&(!eligible||!focused_))||owns!=owns_input_){started_.reset();if(owns||owns_input_)blocked_=held();}
        movie_=generation;eligible_=generation&&eligible&&focused_;
        owns_input_=owns;
        if(!held())blocked_=false;
        (void)now;
    }
    // True only for Confirm edges owned by the movie, including its final
    // release after completion. Other controls keep their normal owner.
    bool event(const SDL_Event& e,SDL_JoystickID selected,std::uint64_t now){
        const bool was_held=held(),owned=owns_input_||started_.has_value()||blocked_;
        const auto keyboard=input_bindings().keyboard[unsigned(InputAction::confirm)],controller=input_bindings().joystick[unsigned(InputAction::confirm)];
        if(keyboard!=keyboard_||controller!=controller_||selected!=selected_||((e.type==SDL_EVENT_GAMEPAD_REMOVED||e.type==SDL_EVENT_JOYSTICK_REMOVED)&&e.gdevice.which==selected_)){
            started_.reset();blocked_=was_held;
            if(keyboard!=keyboard_||controller!=controller_)bindings_.reset();else if(selected!=selected_)bindings_.reset(1);
            keyboard_=keyboard;controller_=controller;selected_=selected;
        }
        bool confirm=false;for(const auto edge:bindings_.event(e,selected))confirm|=edge.action==InputAction::confirm;
        if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST){focused_=false;started_.reset();blocked_=false;return false;}
        if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED){focused_=true;started_.reset();return false;}
        const bool is_held=held();
        if(!is_held){started_.reset();blocked_=false;}
        else if(!was_held&&eligible_&&!blocked_)started_=now;
        return confirm&&owned;
    }
    bool take_skip(std::uint64_t now){
        if(!eligible_||!started_||now<*started_||now-*started_<duration_ms)return false;
        started_.reset();blocked_=true;return true;
    }
    float progress(std::uint64_t now)const{
        if(!eligible_||!started_||now<*started_)return 0;
        return std::min(1.f,float(now-*started_)/float(duration_ms));
    }
    bool visible()const{return eligible_&&started_.has_value();}
};
}

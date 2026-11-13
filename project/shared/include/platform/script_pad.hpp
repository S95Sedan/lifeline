#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <deque>
#include <algorithm>
#include "platform/bindings.hpp"
namespace lifeline {
class ScriptPad {
    std::uint16_t keyboard_=0,gamepad_=0;
    SDL_JoystickID gamepad_id_=0;
    std::array<std::uint16_t,2> escape_{0x2000,0x2000},enter_{0x4000,0x4000};
    BindingState bindings_;
    std::array<std::array<std::uint16_t,input_action_count>,2> masks_{};
    int direction_edge_=-1;
    bool title_pressed_=false;
    std::deque<bool> script_confirmations_;
public:
    struct Sample {std::uint16_t buttons;bool pause;};
private:
    std::deque<Sample> transitions_;
    static bool equal(Sample a,Sample b){return a.buttons==b.buttons&&a.pause==b.pause;}
    void enqueue(Sample value){
        if(!transitions_.empty()&&equal(transitions_.back(),value))return;
        // Keep short taps, but do not replay a stalled event pump's old
        // input for seconds after the player has released the controls.
        constexpr std::size_t limit=8;
        if(transitions_.size()==limit)transitions_.pop_front();
        transitions_.push_back(value);
    }
    bool pause_held()const{
        for(const auto& source:masks_)if(source[unsigned(InputAction::pause)]||source[unsigned(InputAction::back)]==8)return true;
        return false;
    }
public:
    static std::uint16_t confirmation(std::uint16_t requested,bool frontend,bool paused){
        if(paused)return 0x8000;
        if(frontend)return 0x4000;
        if(requested&8)return 8;
        return requested&0x8000?0x8000:0x4000;
    }
    void event(const SDL_Event&,SDL_JoystickID selected,std::uint16_t back=0x2000,std::uint16_t confirm=0x4000,bool any_button=false);
    bool take_script_confirmation(){
        if(script_confirmations_.empty())return false;
        const bool result=script_confirmations_.front();script_confirmations_.pop_front();return result;
    }
    std::uint16_t held()const{return keyboard_|gamepad_;}
    int direction_edge()const{return direction_edge_;}
    // A newly started fullscreen movie owns Confirm, including a press queued
    // just before it appeared. Keep the independent Pause intent intact.
    void suppress_confirmation(){
        const auto mask=std::uint16_t(enter_[0]|enter_[1]);
        for(unsigned source=0;source<2;++source){masks_[source][unsigned(InputAction::confirm)]=0;bindings_.release(InputAction::confirm,source);}
        keyboard_=gamepad_=0;for(auto value:masks_[0])keyboard_|=value;for(auto value:masks_[1])gamepad_|=value;
        for(auto& transition:transitions_)transition.buttons=std::uint16_t((transition.buttons&~mask)|(transition.pause?8:0));
        transitions_.erase(std::unique(transitions_.begin(),transitions_.end(),equal),transitions_.end());script_confirmations_.clear();
    }
    // A frontend menu action has already handled this button. Do not deliver
    // the same press again to the script as a different PS2 action.
    void consume(std::uint16_t mask){
        keyboard_&=~mask;gamepad_&=~mask;
        for(auto& source:masks_)for(auto& value:source)value&=~mask;
        for(auto& transition:transitions_){transition.buttons&=~mask;if(mask&8)transition.pause=false;}
        transitions_.erase(std::unique(transitions_.begin(),transitions_.end(),equal),transitions_.end());
    }
    // Sample once per game tick. Preserve taps received entirely between ticks,
    // including their release so a subsequent press is a new script edge.
    Sample sample_state(){
        if(transitions_.empty())return {held(),pause_held()};
        const auto value=transitions_.front();transitions_.pop_front();return value;
    }
    std::uint16_t sample(){return sample_state().buttons;}
};
}

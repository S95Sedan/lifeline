#include "platform/script_pad.hpp"
#include <algorithm>
namespace lifeline {
void ScriptPad::event(const SDL_Event& e,SDL_JoystickID selected,std::uint16_t back,std::uint16_t confirm,bool any_button){
    direction_edge_=-1;
    if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST){keyboard_=gamepad_=0;masks_={};bindings_.reset();script_confirmations_.clear();transitions_.clear();title_pressed_=false;return;}
    if((e.type==SDL_EVENT_GAMEPAD_REMOVED||e.type==SDL_EVENT_JOYSTICK_REMOVED) && e.gdevice.which==gamepad_id_){
        gamepad_=0;masks_[1]={};bindings_.reset(1);gamepad_id_=0;title_pressed_=false;
        // Discard pending controller input even if MenuInput already selected
        // a replacement controller before forwarding this removal event.
        transitions_.clear();script_confirmations_.clear();return;
    }
    if(any_button){
        if(!title_pressed_&&is_button_press(e,selected)){
            keyboard_=gamepad_=0;masks_={};bindings_.reset();script_confirmations_.clear();transitions_.clear();
            // One pulse opens the title menu. Holding/releasing the original
            // key must not become navigation or confirmation in that menu.
            enqueue({confirm,false});enqueue({0,false});title_pressed_=true;
            if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN)gamepad_id_=selected;
        }
        return;
    }
    title_pressed_=false;
    const auto before=held();
    const auto paused_before=pause_held();
    constexpr std::uint16_t actions[]{0x10,0x40,0x80,0x20,0x4000,0x2000,8,0x2000,0x400,0x800,0x200};
    for(const auto edge:bindings_.event(e,selected)){
        auto mask=actions[unsigned(edge.action)];if(edge.source)gamepad_id_=selected;
        if(edge.action==InputAction::confirm){if(edge.down)enter_[edge.source]=confirm;mask=enter_[edge.source];if(!edge.down)script_confirmations_.push_back(mask!=0x4000);}
        if(edge.action==InputAction::back){if(edge.down)escape_[edge.source]=edge.source?0x2000:back;mask=escape_[edge.source];}
        masks_[edge.source][unsigned(edge.action)]=edge.down?mask:0;
        if(edge.down&&unsigned(edge.action)<4)direction_edge_=int(edge.action);
    }
    keyboard_=gamepad_=0;for(auto m:masks_[0])keyboard_|=m;for(auto m:masks_[1])gamepad_|=m;
    if(held()!=before||pause_held()!=paused_before)enqueue({held(),pause_held()});
}
}

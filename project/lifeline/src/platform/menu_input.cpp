#include "platform/menu_input.hpp"
#include <algorithm>

namespace lifeline {
MenuInput::MenuInput() {open_gamepad();}
void MenuInput::open_gamepad() {
    if(gamepad_||joystick_) return;
    int count=0;std::unique_ptr<SDL_JoystickID,decltype(&SDL_free)> ids(SDL_GetGamepads(&count),SDL_free);
    for(int i=0;ids && i<count;++i) if(auto* pad=SDL_OpenGamepad(ids.get()[i])) {gamepad_.reset(pad);break;}
    if(!gamepad_){ids.reset(SDL_GetJoysticks(&count));for(int i=0;ids&&i<count;++i)if(!SDL_IsGamepad(ids.get()[i]))if(auto* joy=SDL_OpenJoystick(ids.get()[i])){joystick_.reset(joy);break;}}
}
void MenuInput::directions(std::uint64_t now,bool emit) {
    const auto pressed=[&](Control control) {return std::any_of(held_.begin(),held_.end(),[&](const auto& source){return source[unsigned(control)];});};
    const std::array<int,2> direction{int(pressed(Control::right))-int(pressed(Control::left)),int(pressed(Control::down))-int(pressed(Control::up))};
    for(unsigned axis=0;axis<2;++axis) if(direction[axis]!=direction_[axis]) {
        direction_[axis]=direction[axis];next_repeat_[axis]=now+350;
        if(emit && direction[axis]) actions_.push_back(axis?(direction[axis]<0?MenuAction::up:MenuAction::down):(direction[axis]<0?MenuAction::left:MenuAction::right));
    }
}
void MenuInput::set(unsigned source,Control control,bool down,std::uint64_t now) {
    auto& held=held_[source][unsigned(control)];if(held==down) return;held=down;
    if(control<=Control::right) {directions(now,true);return;}
    if(!down && (control==Control::confirm || control==Control::start || control==Control::alternate)) actions_.push_back(MenuAction::confirm);
    if(down) switch(control) {
        case Control::back:actions_.push_back(MenuAction::back);break;
        case Control::escape:actions_.push_back(MenuAction::escape);break;
        case Control::pause:actions_.push_back(MenuAction::pause);break;
        case Control::restart:actions_.push_back(MenuAction::restart);break;
        default:break;
    }
}
void MenuInput::clear_gamepad(std::uint64_t now) {held_[1]={};held_[2]={};bindings_.reset(1);directions(now,false);}
void MenuInput::reset() {held_={};direction_={};next_repeat_={};actions_.clear();bindings_.reset();}
void MenuInput::event(const SDL_Event& event,std::uint64_t now,bool any_button) {
    if(any_button!=any_button_){reset();any_button_=any_button;}
    observe_prompt_input(event,gamepad_id());
    // SDL can emit synthetic button releases before its removal event. They
    // must not confirm a menu when a controller is unplugged while held.
    if(gamepad_ && !SDL_GamepadConnected(gamepad_.get()))clear_gamepad(now);
    if(joystick_ && !SDL_JoystickConnected(joystick_.get()))clear_gamepad(now);
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST) {reset();return;}
    if(event.type==SDL_EVENT_GAMEPAD_ADDED||event.type==SDL_EVENT_JOYSTICK_ADDED) {open_gamepad();return;}
    if((event.type==SDL_EVENT_GAMEPAD_REMOVED||event.type==SDL_EVENT_JOYSTICK_REMOVED) && event.gdevice.which==gamepad_id()) {
        clear_gamepad(now);gamepad_.reset();joystick_.reset();open_gamepad();return;
    }
    if(any_button){
        if(is_button_press(event,gamepad_id()))actions_.push_back(MenuAction::confirm);
        return;
    }
    if(!fixed_navigation_)for(const auto edge:bindings_.event(event,gamepad_id())){
        std::optional<Control> control;
        switch(edge.action){case InputAction::up:control=Control::up;break;case InputAction::down:control=Control::down;break;case InputAction::left:control=Control::left;break;case InputAction::right:control=Control::right;break;case InputAction::confirm:control=Control::confirm;break;case InputAction::back:control=Control::back;break;case InputAction::pause:control=Control::pause;break;default:break;}
        if(control)set(edge.source,*control,edge.down,now);
    }
    if(fixed_navigation_&&(event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP)) {
        if(event.key.repeat) return;
        std::optional<Control> control;
        switch(event.key.key) {
            case SDLK_UP:control=Control::up;break;case SDLK_DOWN:control=Control::down;break;
            case SDLK_LEFT:control=Control::left;break;case SDLK_RIGHT:control=Control::right;break;
            case SDLK_RETURN:control=Control::confirm;break;
            case SDLK_ESCAPE:control=Control::back;break;
            default:break;
        }
        if(control) set(0,*control,event.type==SDL_EVENT_KEY_DOWN,now);
    }
    if(fixed_navigation_&&(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type==SDL_EVENT_GAMEPAD_BUTTON_UP) && gamepad_ && SDL_GamepadConnected(gamepad_.get()) && event.gbutton.which==gamepad_id()) {
        std::optional<Control> control;
        switch(event.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP:control=Control::up;break;case SDL_GAMEPAD_BUTTON_DPAD_DOWN:control=Control::down;break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:control=Control::left;break;case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:control=Control::right;break;
            case SDL_GAMEPAD_BUTTON_SOUTH:control=Control::confirm;break;case SDL_GAMEPAD_BUTTON_START:control=Control::start;break;
            case SDL_GAMEPAD_BUTTON_WEST:control=Control::alternate;break;case SDL_GAMEPAD_BUTTON_EAST:control=Control::back;break;
            default:break;
        }
        if(control)set(1,*control,event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN,now);
    }
    if(fixed_navigation_&&event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION && gamepad_ && SDL_GamepadConnected(gamepad_.get()) && event.gaxis.which==gamepad_id() &&
        (event.gaxis.axis==SDL_GAMEPAD_AXIS_LEFTX || event.gaxis.axis==SDL_GAMEPAD_AXIS_LEFTY)) {
        const auto negative=event.gaxis.axis==SDL_GAMEPAD_AXIS_LEFTX?Control::left:Control::up;
        const auto positive=event.gaxis.axis==SDL_GAMEPAD_AXIS_LEFTX?Control::right:Control::down;
        // Hysteresis avoids repeated navigation when a stick drifts near its threshold.
        const auto value=event.gaxis.value;
        const bool low=value<-(held_[2][unsigned(negative)]?11000:18000),high=value>(held_[2][unsigned(positive)]?11000:18000);
        held_[2][unsigned(negative)]=low;held_[2][unsigned(positive)]=high;directions(now,true);
    }
    if(fixed_navigation_&&joystick_&&(event.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN||event.type==SDL_EVENT_JOYSTICK_BUTTON_UP)&&event.jbutton.which==gamepad_id()){
        if(event.jbutton.button==0)set(1,Control::confirm,event.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN,now);if(event.jbutton.button==1)set(1,Control::back,event.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN,now);
    }
    if(fixed_navigation_&&joystick_&&event.type==SDL_EVENT_JOYSTICK_AXIS_MOTION&&event.jaxis.which==gamepad_id()&&event.jaxis.axis<2){
        const auto negative=event.jaxis.axis==0?Control::left:Control::up,positive=event.jaxis.axis==0?Control::right:Control::down;
        held_[2][unsigned(negative)]=event.jaxis.value<-(held_[2][unsigned(negative)]?11000:18000);held_[2][unsigned(positive)]=event.jaxis.value>(held_[2][unsigned(positive)]?11000:18000);directions(now,true);
    }
    if(fixed_navigation_&&joystick_&&event.type==SDL_EVENT_JOYSTICK_HAT_MOTION&&event.jhat.which==gamepad_id()&&event.jhat.hat==0){
        held_[2][unsigned(Control::up)]=(event.jhat.value&SDL_HAT_UP)!=0;held_[2][unsigned(Control::down)]=(event.jhat.value&SDL_HAT_DOWN)!=0;held_[2][unsigned(Control::left)]=(event.jhat.value&SDL_HAT_LEFT)!=0;held_[2][unsigned(Control::right)]=(event.jhat.value&SDL_HAT_RIGHT)!=0;directions(now,true);
    }
}
std::vector<MenuAction> MenuInput::take(std::uint64_t now) {
    for(unsigned axis=0;axis<2;++axis) if(direction_[axis] && now>=next_repeat_[axis]) {
        actions_.push_back(axis?(direction_[axis]<0?MenuAction::up:MenuAction::down):(direction_[axis]<0?MenuAction::left:MenuAction::right));
        next_repeat_[axis]=now+80; // No burst of queued moves after a stalled frame.
    }
    auto result=std::move(actions_);actions_.clear();return result;
}
}

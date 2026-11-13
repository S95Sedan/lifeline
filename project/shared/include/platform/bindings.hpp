#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace lifeline {
enum class InputAction {up,down,left,right,confirm,back,pause,speak,map,items,keywords,count};
constexpr unsigned input_action_count=unsigned(InputAction::count);
enum class BindingKind {none,key,pad_button,pad_axis,joystick_button,joystick_axis,joystick_hat};
struct InputBinding {
    BindingKind kind=BindingKind::none;
    int code=0,direction=0;
    bool operator==(const InputBinding&) const=default;
};
struct InputBindings {
    std::array<InputBinding,input_action_count> keyboard{},joystick{};
    InputBindings();
    bool operator==(const InputBindings&) const=default;
    void assign(InputAction,unsigned column,InputBinding); // Swap conflicts in the same column.
};
InputBindings& input_bindings();
const char* input_action_label(InputAction);
std::string binding_label(const InputBinding&);
std::string action_hint(InputAction);
// Prompts follow the last device used and read bindings live.
void observe_prompt_input(const SDL_Event&,SDL_JoystickID selected);
std::string input_prompt(InputAction);
std::string navigation_prompt();
bool is_button_press(const SDL_Event&,SDL_JoystickID selected);
bool valid_binding(const InputBinding&,unsigned column);
std::optional<InputBinding> capture_binding(const SDL_Event&,unsigned column,SDL_JoystickID selected);
struct InputEdge {InputAction action;bool down;unsigned source;};
class BindingState {
    std::array<std::array<bool,input_action_count>,2> held_{};
    SDL_JoystickID device_=0;
public:
    std::vector<InputEdge> event(const SDL_Event&,SDL_JoystickID selected);
    void reset(){held_={};device_=0;}
    void reset(unsigned source){held_.at(source)={};if(source)device_=0;}
    bool held(InputAction a,unsigned source)const{return held_[source][unsigned(a)];}
    void release(InputAction a,unsigned source){held_[source][unsigned(a)]=false;}
};
}

#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "platform/bindings.hpp"

namespace lifeline {
enum class MenuAction {up,down,left,right,confirm,back,escape,pause,restart};
// Native host input only. Confirmation is released once, while directions repeat.
class MenuInput {
    enum class Control {up,down,left,right,confirm,start,alternate,back,escape,pause,restart,count};
    static constexpr auto controls=static_cast<unsigned>(Control::count);
    std::array<std::array<bool,controls>,3> held_{};
    BindingState bindings_;
    bool fixed_navigation_=false;
    bool any_button_=false;
    std::array<int,2> direction_{};
    std::array<std::uint64_t,2> next_repeat_{};
    std::unique_ptr<SDL_Gamepad,decltype(&SDL_CloseGamepad)> gamepad_{nullptr,SDL_CloseGamepad};
    std::unique_ptr<SDL_Joystick,decltype(&SDL_CloseJoystick)> joystick_{nullptr,SDL_CloseJoystick};
    std::vector<MenuAction> actions_;
    void open_gamepad();
    void set(unsigned source,Control,bool down,std::uint64_t now);
    void directions(std::uint64_t now,bool emit);
    void clear_gamepad(std::uint64_t now);
public:
    MenuInput();
    void event(const SDL_Event&,std::uint64_t now,bool any_button=false);
    std::vector<MenuAction> take(std::uint64_t now);
    void reset();
    void discard_confirmation(){std::erase(actions_,MenuAction::confirm);}
    void fixed_navigation(bool value){if(value!=fixed_navigation_){reset();fixed_navigation_=value;}}
    SDL_JoystickID gamepad_id() const {return gamepad_?SDL_GetGamepadID(gamepad_.get()):joystick_?SDL_GetJoystickID(joystick_.get()):0;}
};
}

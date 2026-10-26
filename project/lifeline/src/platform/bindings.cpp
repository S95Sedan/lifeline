#include "platform/bindings.hpp"
#include <algorithm>
#include <stdexcept>
namespace lifeline {
namespace {bool prompt_pad=false;SDL_GamepadType prompt_type=SDL_GAMEPAD_TYPE_UNKNOWN;SDL_JoystickID prompt_id=0;}
InputBindings::InputBindings(){
    constexpr SDL_Scancode keys[]{SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_RETURN,SDL_SCANCODE_ESCAPE,SDL_SCANCODE_P,SDL_SCANCODE_T,SDL_SCANCODE_M,SDL_SCANCODE_I,SDL_SCANCODE_K};
    constexpr int buttons[]{SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER};
    for(unsigned i=0;i<input_action_count;++i)keyboard[i]={BindingKind::key,keys[i],0};
    for(unsigned i=0;i<std::size(buttons);++i)joystick[i]={BindingKind::pad_button,buttons[i],0};
    joystick[9]={BindingKind::pad_axis,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,1};joystick[10]={BindingKind::pad_axis,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,1};
}
InputBindings& input_bindings(){static InputBindings value;return value;}
bool is_button_press(const SDL_Event& e,SDL_JoystickID selected){
    return (e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat)||
        (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&e.button.button!=0)||
        (e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&selected&&e.gbutton.which==selected)||
        (e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN&&selected&&e.jbutton.which==selected);
}
const char* input_action_label(InputAction a){constexpr const char* labels[]{"Up","Down","Left","Right","Confirm / Operate","Back / Close","Pause","Push to talk","Map","Items","Keywords"};return labels[unsigned(a)];}
bool valid_binding(const InputBinding& b,unsigned column){
    if(b.kind==BindingKind::none)return b.code==0&&b.direction==0;
    if(!column)return b.kind==BindingKind::key&&b.code>SDL_SCANCODE_UNKNOWN&&b.code<SDL_SCANCODE_COUNT&&!b.direction;
    switch(b.kind){
    case BindingKind::pad_button:return b.code>=0&&b.code<SDL_GAMEPAD_BUTTON_COUNT&&!b.direction;
    case BindingKind::pad_axis:return b.code>=0&&b.code<SDL_GAMEPAD_AXIS_COUNT&&(b.direction==1||b.direction==-1);
    case BindingKind::joystick_button:return b.code>=0&&b.code<256&&!b.direction;
    case BindingKind::joystick_axis:return b.code>=0&&b.code<256&&(b.direction==1||b.direction==-1);
    case BindingKind::joystick_hat:return b.code>=0&&b.code<16&&(b.direction==SDL_HAT_UP||b.direction==SDL_HAT_DOWN||b.direction==SDL_HAT_LEFT||b.direction==SDL_HAT_RIGHT);
    default:return false;
    }
}
void InputBindings::assign(InputAction action,unsigned column,InputBinding binding){
    if(column>1||!valid_binding(binding,column))throw std::runtime_error("Invalid control binding");
    auto& list=column?joystick:keyboard;const auto index=unsigned(action);const auto previous=list.at(index);
    if(binding.kind!=BindingKind::none)for(unsigned i=0;i<list.size();++i)if(i!=index&&list[i]==binding)list[i]=previous;
    list[index]=binding;
}
std::string binding_label(const InputBinding& b){
    switch(b.kind){
    case BindingKind::none:return "Unbound";
    case BindingKind::key:{const auto* name=SDL_GetScancodeName(SDL_Scancode(b.code));return name&&*name?name:"Key "+std::to_string(b.code);}
    case BindingKind::pad_button:{constexpr const char* names[]{"A / Cross","B / Circle","X / Square","Y / Triangle","Back","Guide","Start","Left stick","Right stick","LB / L1","RB / R1","D-pad Up","D-pad Down","D-pad Left","D-pad Right"};return b.code<int(std::size(names))?names[b.code]:"Pad button "+std::to_string(b.code+1);}
    case BindingKind::pad_axis:{constexpr const char* names[]{"Left stick X","Left stick Y","Right stick X","Right stick Y","LT / L2","RT / R2"};return std::string(names[b.code])+(b.code<4?(b.direction<0?" -":" +"):"");}
    case BindingKind::joystick_button:return "Button "+std::to_string(b.code+1);
    case BindingKind::joystick_axis:return "Axis "+std::to_string(b.code+1)+(b.direction<0?" -":" +");
    case BindingKind::joystick_hat:return "Hat "+std::to_string(b.code+1)+" "+(b.direction==SDL_HAT_UP?"Up":b.direction==SDL_HAT_DOWN?"Down":b.direction==SDL_HAT_LEFT?"Left":"Right");
    }return {};
}
void observe_prompt_input(const SDL_Event& e,SDL_JoystickID selected){
    if(e.type==SDL_EVENT_KEY_DOWN||e.type==SDL_EVENT_MOUSE_BUTTON_DOWN||e.type==SDL_EVENT_MOUSE_WHEEL){prompt_pad=false;return;}
    if((e.type==SDL_EVENT_GAMEPAD_REMOVED||e.type==SDL_EVENT_JOYSTICK_REMOVED)&&e.gdevice.which==prompt_id){prompt_pad=false;prompt_id=0;return;}
    if(!selected)return;
    const bool button=(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&e.gbutton.which==selected)||(e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN&&e.jbutton.which==selected);
    const bool axis=(e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&e.gaxis.which==selected&&std::abs(int(e.gaxis.value))>18000)||(e.type==SDL_EVENT_JOYSTICK_AXIS_MOTION&&e.jaxis.which==selected&&!SDL_IsGamepad(selected)&&std::abs(int(e.jaxis.value))>18000);
    const bool hat=e.type==SDL_EVENT_JOYSTICK_HAT_MOTION&&e.jhat.which==selected&&e.jhat.value!=SDL_HAT_CENTERED;
    if(button||axis||hat){prompt_pad=true;prompt_id=selected;prompt_type=SDL_GetGamepadTypeForID(selected);}
}
std::string input_prompt(InputAction action){
    const auto& b=prompt_pad?input_bindings().joystick.at(unsigned(action)):input_bindings().keyboard.at(unsigned(action));
    if(!prompt_pad||b.kind==BindingKind::none){if(b.kind==BindingKind::key&&b.code==SDL_SCANCODE_RETURN)return "Enter";return binding_label(b);}
    const bool ps=prompt_type==SDL_GAMEPAD_TYPE_PS3||prompt_type==SDL_GAMEPAD_TYPE_PS4||prompt_type==SDL_GAMEPAD_TYPE_PS5;
    const bool nintendo=prompt_type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO||prompt_type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT||prompt_type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT||prompt_type==SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    if(b.kind==BindingKind::pad_button){
        constexpr const char* generic[]{"A","B","X","Y","Back","Guide","Start","Left stick","Right stick","LB","RB","D-pad Up","D-pad Down","D-pad Left","D-pad Right"};
        constexpr const char* playstation[]{"Cross","Circle","Square","Triangle","Share","PS","Options","L3","R3","L1","R1"};
        constexpr const char* switched[]{"B","A","Y","X","Minus","Home","Plus"};
        if(ps&&b.code<int(std::size(playstation))){if(prompt_type==SDL_GAMEPAD_TYPE_PS3&&b.code==SDL_GAMEPAD_BUTTON_BACK)return "Select";if(prompt_type==SDL_GAMEPAD_TYPE_PS3&&b.code==SDL_GAMEPAD_BUTTON_START)return "Start";if(prompt_type==SDL_GAMEPAD_TYPE_PS5&&b.code==SDL_GAMEPAD_BUTTON_BACK)return "Create";return playstation[b.code];}
        if(nintendo&&b.code<int(std::size(switched)))return switched[b.code];
        if(nintendo&&b.code==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)return "L";
        if(nintendo&&b.code==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)return "R";
        return b.code<int(std::size(generic))?generic[b.code]:"Pad button "+std::to_string(b.code+1);
    }
    if(b.kind==BindingKind::pad_axis&&b.code>=SDL_GAMEPAD_AXIS_LEFT_TRIGGER)return b.code==SDL_GAMEPAD_AXIS_LEFT_TRIGGER?(ps?"L2":nintendo?"ZL":"LT"):(ps?"R2":nintendo?"ZR":"RT");
    return binding_label(b);
}
std::string navigation_prompt(){
    const auto up=input_prompt(InputAction::up),down=input_prompt(InputAction::down),left=input_prompt(InputAction::left),right=input_prompt(InputAction::right);
    if(up=="Up"&&down=="Down"&&left=="Left"&&right=="Right")return "Arrow keys";
    if(up=="D-pad Up"&&down=="D-pad Down"&&left=="D-pad Left"&&right=="D-pad Right")return "D-pad";
    return up+" / "+down+" / "+left+" / "+right;
}
std::string action_hint(InputAction action){return input_prompt(action);}
std::optional<InputBinding> capture_binding(const SDL_Event& e,unsigned col,SDL_JoystickID id){
    if(!col){if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat&&e.key.scancode!=SDL_SCANCODE_ESCAPE&&e.key.scancode!=SDL_SCANCODE_DELETE)return InputBinding{BindingKind::key,int(e.key.scancode),0};return {};}
    if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&id&&e.gbutton.which==id)return InputBinding{BindingKind::pad_button,e.gbutton.button,0};
    if(e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&id&&e.gaxis.which==id&&std::abs(int(e.gaxis.value))>22000)return InputBinding{BindingKind::pad_axis,e.gaxis.axis,e.gaxis.value<0?-1:1};
    // Mapped devices also emit raw events; capture only their named gamepad events.
    if(id&&SDL_IsGamepad(id))return {};
    if(e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN&&id&&e.jbutton.which==id)return InputBinding{BindingKind::joystick_button,e.jbutton.button,0};
    if(e.type==SDL_EVENT_JOYSTICK_AXIS_MOTION&&id&&e.jaxis.which==id&&std::abs(int(e.jaxis.value))>22000)return InputBinding{BindingKind::joystick_axis,e.jaxis.axis,e.jaxis.value<0?-1:1};
    if(e.type==SDL_EVENT_JOYSTICK_HAT_MOTION&&id&&e.jhat.which==id)for(int d:{SDL_HAT_UP,SDL_HAT_DOWN,SDL_HAT_LEFT,SDL_HAT_RIGHT})if(e.jhat.value&d)return InputBinding{BindingKind::joystick_hat,e.jhat.hat,d};
    return {};
}
std::vector<InputEdge> BindingState::event(const SDL_Event& e,SDL_JoystickID id){
    std::vector<InputEdge> edges;
    if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST){reset();return edges;}
    if(e.type==SDL_EVENT_GAMEPAD_REMOVED||e.type==SDL_EVENT_JOYSTICK_REMOVED){if(e.gdevice.which==device_){held_[1]={};device_=0;}return edges;}
    const auto& bindings=input_bindings();
    for(unsigned source=0;source<2;++source)for(unsigned i=0;i<input_action_count;++i){
        const auto& b=(source?bindings.joystick:bindings.keyboard)[i];std::optional<bool> down;
        if(b.kind==BindingKind::key&&(e.type==SDL_EVENT_KEY_DOWN||e.type==SDL_EVENT_KEY_UP)&&!e.key.repeat){
            // Synthetic tests and old host messages may omit a scancode.
            const auto scan=e.key.scancode?e.key.scancode:SDL_GetScancodeFromKey(e.key.key,nullptr);
            if(scan==b.code)down=e.type==SDL_EVENT_KEY_DOWN;
        }
        if(b.kind==BindingKind::pad_button&&(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||e.type==SDL_EVENT_GAMEPAD_BUTTON_UP)&&id&&e.gbutton.which==id&&e.gbutton.button==b.code)down=e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if(b.kind==BindingKind::pad_axis&&e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&id&&e.gaxis.which==id&&e.gaxis.axis==b.code)down=int(e.gaxis.value)*b.direction>(held_[source][i]?11000:18000);
        if(b.kind==BindingKind::joystick_button&&(e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN||e.type==SDL_EVENT_JOYSTICK_BUTTON_UP)&&id&&e.jbutton.which==id&&e.jbutton.button==b.code)down=e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN;
        if(b.kind==BindingKind::joystick_axis&&e.type==SDL_EVENT_JOYSTICK_AXIS_MOTION&&id&&e.jaxis.which==id&&e.jaxis.axis==b.code)down=int(e.jaxis.value)*b.direction>(held_[source][i]?11000:18000);
        if(b.kind==BindingKind::joystick_hat&&e.type==SDL_EVENT_JOYSTICK_HAT_MOTION&&id&&e.jhat.which==id&&e.jhat.hat==b.code)down=(e.jhat.value&b.direction)!=0;
        if(down&&*down!=held_[source][i]){if(source)device_=id;held_[source][i]=*down;edges.push_back({InputAction(i),*down,source});}
    }return edges;
}
}

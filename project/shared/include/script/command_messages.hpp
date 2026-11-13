#pragma once
#include "script/runtime.hpp"
#include "script/command_modes.hpp"
#include <deque>
#include <stdexcept>

namespace lifeline::script {
// Recovered command-manager messages needed by the opening conversation.
// Unknown protocols are reported instead of silently accepting them.
class CommandMessages {
    struct Message {std::int32_t type,first,second;};
    std::deque<Message> messages_;
    std::int32_t group_=0,display_=0;
    unsigned microphone_score_=0;
    std::function<void()> reset_handler_;
    std::function<void(bool)> microphone_handler_;
    std::function<void(int)> group_handler_;
    // Original constructor 164f20 clears panel visibility +f4.
    bool microphone_enabled_=false;
    CommandModes* modes_=nullptr;
    int microphone_y_=-220;
    int owner_=-1;
public:
    void modes(CommandModes& modes){modes_=&modes;}
    void post(int type,int first,int second);
    void microphone_position(int x,int y){if(x!=-320)throw std::runtime_error("Microphone horizontal placement not connected");microphone_y_=y;}
    int microphone_y()const{return microphone_y_;}
    void on_reset(std::function<void()> handler){reset_handler_=std::move(handler);}
    void on_microphone(std::function<void(bool)> handler){microphone_handler_=std::move(handler);}
    void on_group(std::function<void(int)> handler){group_handler_=std::move(handler);}
    Bindings bindings();
    void advance();
    std::int32_t group()const{return group_;}
    int owner()const{return owner_;}
    std::int32_t display()const{return display_;}
    bool microphone_enabled()const{return microphone_enabled_;}
    unsigned microphone_score()const{return microphone_score_;}
};
}

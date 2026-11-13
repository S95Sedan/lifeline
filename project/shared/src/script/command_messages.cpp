#include "script/command_messages.hpp"
#include <cmath>
#include <stdexcept>
namespace lifeline::script {
void CommandMessages::post(int type,int first,int second){
    if(messages_.size()==8)throw std::runtime_error("Command message queue is full");messages_.push_back({type,first,second});
}
Bindings CommandMessages::bindings(){
    Bindings result{{1107,Binding{3,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value) || value!=std::trunc(value) || value< -2147483648. || value>2147483647.)throw std::runtime_error("Invalid command message");
        // 16a3d0 handles reset synchronously, before ordinary queue dispatch.
        // 16a948 clears pending messages; analysis and command displays reset
        // through the subscriber broadcast at 16a480.
        if(c.arguments[0]==34003){
            messages_.clear();group_=0;display_=0;microphone_enabled_=false;microphone_y_=-220;owner_=-1;
            if(microphone_handler_)microphone_handler_(false);
            if(modes_)modes_->reset();
            if(reset_handler_)reset_handler_();
            return Reply{false,1.f};
        }
        post(std::int32_t(c.arguments[0]),std::int32_t(c.arguments[1]),std::int32_t(c.arguments[2]));return Reply{false,1.f};
    }}}};
    // 103120/1031c0 discard one argument and return one. 1030d8/103178
    // return zero without arguments. No subscriber operation occurs here.
    for(unsigned opcode:{1103u,1105u})result.emplace(opcode,Binding{1,0,[](const Invocation&){return Reply{false,1.f};}});
    for(unsigned opcode:{1104u,1106u})result.emplace(opcode,Binding{0,0,[](const Invocation&){return Reply{false,0.f};}});
    if(modes_){
        result.emplace(1101,Binding{0,0,[this](const Invocation&){return Reply{false,float(modes_->focused())};}});
        result.emplace(1102,Binding{1,0,[this](const Invocation& c){
            const auto mode=c.arguments[0];if(!std::isfinite(mode)||double(mode)<-2147483648.||double(mode)>2147483647.)throw std::runtime_error("Invalid command mode");
            return Reply{false,modes_->change(int(mode))?1.f:0.f};
        }});
    }
    return result;
}
void CommandMessages::advance(){
    while(!messages_.empty()){
        const auto message=messages_.front();
        switch(message.type){
        case 34006:owner_=message.first;break;
        case 34002:if(!modes_)throw std::runtime_error("Command modes are not connected");modes_->input(message.first,message.second);break;
        case 34007:if(!modes_)throw std::runtime_error("Command modes are not connected");modes_->ready(unsigned(message.first),message.second!=0);break;
        case 34008:if(!modes_)throw std::runtime_error("Command modes are not connected");modes_->state(message.first);break;
        case 35005:
            group_=message.first;if(group_handler_)group_handler_(group_);
            if(modes_&&!modes_->records().empty()&&modes_->records().back().state==0)modes_->change(group_?1:0);
            break;
        // 1651d8..165224 stores the latest speech score / 10000 at +e4.
        // This is independent of panel state and persistent score averages.
        case 36000:microphone_score_=std::uint32_t(message.first);break;
        case 36001:display_=message.first;break;
        // 165230 sets both microphone-panel +f4 and speech-input +90.
        case 36002:microphone_enabled_=message.first!=0;if(microphone_handler_)microphone_handler_(microphone_enabled_);break;
        // 40000 is broadcast by this script but has no handler in the opening
        // command manager (16a6d0) or its analysis/control subscribers.
        case 40000:break;
        default:throw std::runtime_error("Command message protocol not recovered: "+std::to_string(message.type));
        }
        messages_.pop_front();
    }
}
}

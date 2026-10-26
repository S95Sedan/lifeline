#include "script/command_modes.hpp"
#include <stdexcept>

namespace lifeline::script {
void CommandModes::reset(){flags_.fill(0);flags_[0]=2;stack_.assign(1,Record{});}
bool CommandModes::input(int action,int parameter){
    if(stack_.empty()||(stack_.back().state!=0&&stack_.back().state!=3))return false;
    // Manager 16ab30 consumes map-toggle/back before forwarding input to
    // the focused subscriber. Navigator itself intentionally ignores buttons.
    const auto record=stack_.back();
    if(action==8 || action==9 || action==10){
        const int target=action-5; // 16ad20 / 16adbc / 16ae58: items, keywords, map.
        if(record.state!=3)change(record.mode==target?0:target);
        return true;
    }
    if(action==5 && (record.mode==3 || record.mode==4 || record.mode==5)){
        change(0);return true;
    }
    const int mode=focused();if(mode<0)return false;send(mode,34002,action,parameter);return true;
}
void CommandModes::overlay(unsigned mode,bool active,bool focus,int first,int second){
    if(active){
        if(focus)for(auto& flag:flags_)flag&=~1u;
        flags_.at(mode)=(flags_.at(mode)|2u)&~1u;if(focus)flags_[mode]|=1;
        send(int(mode),34000,first,second);
    }else{send(int(mode),34001,first,second);for(auto& flag:flags_)flag&=~1u;}
}
void CommandModes::send(int mode,int message,int first,int second){
    if(mode<0 || unsigned(mode)>=subscribers_.size() || !subscribers_[mode].message)
        throw std::runtime_error("Command interface subscriber not connected: "+std::to_string(mode));
    subscribers_[mode].message(message,first,second);
}
bool CommandModes::change(int mode,int first,int second){
    if(stack_.empty()){reset();return false;}
    const auto old=stack_.back();
    if(old.mode==mode || old.state==2 || old.state==3 || mode<0 || mode>=7)return false;
    if(subscribers_[mode].available&&!subscribers_[mode].available())return false;
    if(old.mode<0 || old.mode>=7)return true;
    // Validate the destination before releasing the currently usable interface.
    if(!subscribers_[mode].message || !subscribers_[mode].advance)
        throw std::runtime_error("Command interface subscriber not connected: "+std::to_string(mode));
    stack_.pop_back();
    Record transition{2,old.mode,old.mode,mode,first,second};
    if(old.mode==0){
        transition.previous=-1;transition.first=old.first;transition.second=old.second;
        send(mode,34000,first,second);flags_[mode]|=2;
    }else send(old.mode,34001,0,0);
    if(stack_.size()<4)stack_.push_back({mode==6?1:0,mode,-1,-1,first,second});
    if(stack_.size()<4)stack_.push_back(transition);
    return true;
}
void CommandModes::advance(){
    // Original subscriber order matters: a later destination may update in the
    // same tick that an earlier source reports it has finished closing.
    for(unsigned i=0;i<subscribers_.size();++i){
        if(!(flags_[i]&2))continue;
        if(!subscribers_[i].advance)throw std::runtime_error("Command interface update not connected: "+std::to_string(i));
        if(subscribers_[i].advance())continue;
        flags_[i]&=~2u;
        if(stack_.empty()){reset();return;}
        const auto transition=stack_.back();
        if(transition.state!=2 || transition.previous!=int(i) || transition.next==-1)continue;
        send(transition.next,34000,transition.first,transition.second);
        flags_.at(unsigned(transition.next))|=2;
    }
}
void CommandModes::ready(unsigned mode,bool focused){
    if(mode>=flags_.size())throw std::runtime_error("Invalid command readiness subscriber");
    // Protocol 34007 pops the transition, then sets the sole focused subscriber.
    if(!stack_.empty())stack_.pop_back();
    for(auto& flag:flags_)flag&=~1u;
    if(focused)flags_[mode]|=1;
}
int CommandModes::focused()const{
    for(unsigned i=0;i<subscribers_.size();++i)if(subscribers_[i].message && (flags_[i]&3)==3)return int(i);
    return -1;
}
}

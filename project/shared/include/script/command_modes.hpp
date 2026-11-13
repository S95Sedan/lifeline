#pragma once
#include "script/runtime.hpp"
#include <array>

namespace lifeline::script {
// Native command-manager transition records recovered from 16b3e8 and 16b170.
// Subscribers own their authored UI timelines; readiness is never timed here.
class CommandModes {
public:
    struct Subscriber {
        std::function<void(int,int,int)> message;
        std::function<bool()> advance;
        std::function<bool()> available;
    };
    struct Record {int state=0,mode=0,previous=-1,next=-1,first=0,second=0;};
private:
    std::array<Subscriber,16> subscribers_;
    std::array<unsigned,16> flags_{};
    std::vector<Record> stack_;
    void send(int mode,int message,int first,int second);
public:
    CommandModes(){reset();}
    void subscriber(unsigned index,Subscriber value){subscribers_.at(index)=std::move(value);}
    void reset();
    bool change(int mode,int first=0,int second=0);
    bool input(int action,int parameter=0);
    void overlay(unsigned mode,bool active,bool focus,int first=0,int second=0);
    void focus(unsigned mode){if(active(mode))flags_.at(mode)|=1;}
    void advance();
    void ready(unsigned mode,bool focused);
    void state(int value){if(stack_.empty())reset();else stack_.back().state=value;}
    int focused()const;
    std::span<const Record> records()const{return stack_;}
    bool active(unsigned mode)const{return (flags_.at(mode)&2)!=0;}
};
}
